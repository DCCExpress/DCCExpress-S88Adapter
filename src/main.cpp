#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <avr/wdt.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "S88AdapterConfig.h"

// -----------------------------------------------------------------------------
// DCCExpress S88Adapter - v0.5.0
//
// Ownership model:
//   * USB serial configures I2C address + S88 byte length.
//   * SAVE persists those values to EEPROM.
//   * The Hub NEVER changes adapter configuration.
//   * The Hub selects INFO, reads the fixed INFO packet, then requests exactly
//     the advertised number of S88 snapshot bytes.
//
// I2C INFO selector (Hub -> adapter write):
//   [0] 0xA5 magic
//   [1] 0x02 INFO request
//   [2] XOR checksum of bytes 0..1 (0xA7)
//
// I2C INFO response (adapter -> Hub, 10 bytes):
//   [0] 0xA5 magic
//   [1] 0x82 INFO response
//   [2] protocol version
//   [3] firmware major
//   [4] firmware minor
//   [5] firmware patch
//   [6] active S88 byte count
//   [7] maximum supported byte count
//   [8] capability flags
//   [9] XOR checksum of bytes 0..8
//
// Any normal I2C read returns the raw S88 snapshot. INFO selection applies to
// the next read only, then the response automatically returns to snapshot mode.
// -----------------------------------------------------------------------------

namespace Config {

constexpr uint32_t SERIAL_BAUD = 115200;

constexpr uint8_t S88_CLOCK_PIN = 2;
constexpr uint8_t S88_LOAD_PIN = 3;
constexpr uint8_t S88_RESET_PIN = 4;
constexpr uint8_t S88_DATA_PIN = 5;

constexpr uint8_t DEFAULT_I2C_ADDRESS = S88_I2C_ADDRESS;
constexpr uint8_t DEFAULT_BYTE_COUNT = S88_DEFAULT_GROUP_COUNT;
constexpr uint8_t MAX_BYTE_COUNT = S88_MAX_GROUP_COUNT;

constexpr uint16_t HALF_CLOCK_US = S88_HALF_CLOCK_US;
constexpr uint16_t CONTROL_PULSE_US = S88_CONTROL_PULSE_US;
constexpr uint32_t READ_INTERVAL_MS = S88_READ_INTERVAL_MS;
constexpr uint32_t LOG_INTERVAL_MS = S88_LOG_INTERVAL_MS;

constexpr uint8_t PROTOCOL_MAGIC = 0xA5;
constexpr uint8_t INFO_REQUEST = 0x02;
constexpr uint8_t INFO_RESPONSE = 0x82;
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint8_t INFO_RESPONSE_SIZE = 10;

// Capabilities reported to the Hub.
constexpr uint8_t CAP_SERIAL_CONFIGURATION = 0x01;
constexpr uint8_t CAP_EEPROM_CONFIGURATION = 0x02;
constexpr uint8_t CAPABILITY_FLAGS =
    CAP_SERIAL_CONFIGURATION |
    CAP_EEPROM_CONFIGURATION;

// Keep EEPROM format version 1 so a v0.4.0 saved configuration upgrades
// without losing address/byte-count settings.
constexpr uint8_t EEPROM_MAGIC_0 = 0xD8;
constexpr uint8_t EEPROM_MAGIC_1 = 0x53;
constexpr uint8_t EEPROM_VERSION = 1;
constexpr int EEPROM_BASE = 0;
constexpr uint8_t EEPROM_SIZE = 6;

constexpr size_t SERIAL_LINE_SIZE = 64;

} // namespace Config

enum class ByteCountSource : uint8_t {
  FirmwareDefault,
  Eeprom,
  Serial
};

enum class I2CResponseMode : uint8_t {
  Snapshot,
  Info
};

static volatile uint8_t s88Snapshot[Config::MAX_BYTE_COUNT] = {};
static volatile uint8_t activeByteCountValue = Config::DEFAULT_BYTE_COUNT;
static volatile ByteCountSource activeByteCountSource = ByteCountSource::FirmwareDefault;
static volatile I2CResponseMode nextI2CResponse = I2CResponseMode::Snapshot;

static uint8_t activeI2CAddress = Config::DEFAULT_I2C_ADDRESS;

// Serial-editable configuration. SET changes RAM, SAVE persists to EEPROM.
static uint8_t stagedI2CAddress = Config::DEFAULT_I2C_ADDRESS;
static uint8_t stagedByteCount = Config::DEFAULT_BYTE_COUNT;

static uint8_t savedI2CAddress = Config::DEFAULT_I2C_ADDRESS;
static uint8_t savedByteCount = Config::DEFAULT_BYTE_COUNT;
static bool eepromConfigurationValid = false;

static bool periodicLoggingEnabled = true;

static uint32_t lastReadMs = 0;
static uint32_t lastLogMs = 0;

static char serialLine[Config::SERIAL_LINE_SIZE] = {};
static size_t serialLineLength = 0;

static inline uint16_t inputCountForBytes(uint8_t bytes) {
  return static_cast<uint16_t>(bytes) * 8U;
}

static uint8_t readActiveByteCount() {
  return activeByteCountValue;
}

static ByteCountSource readActiveByteCountSource() {
  return activeByteCountSource;
}

static inline uint16_t activeInputCount() {
  return inputCountForBytes(readActiveByteCount());
}

static const __FlashStringHelper* byteCountSourceName(ByteCountSource source) {
  switch (source) {
    case ByteCountSource::Eeprom:
      return F("EEPROM");
    case ByteCountSource::Serial:
      return F("SERIAL");
    case ByteCountSource::FirmwareDefault:
    default:
      return F("FIRMWARE_DEFAULT");
  }
}

static inline void waitControlPulse() {
  delayMicroseconds(Config::CONTROL_PULSE_US);
}

static inline void setClock(bool high) {
  digitalWrite(Config::S88_CLOCK_PIN, high ? HIGH : LOW);
  delayMicroseconds(Config::HALF_CLOCK_US);
}

static inline bool readDataPin() {
  return digitalRead(Config::S88_DATA_PIN) == HIGH;
}

static void storeBit(uint8_t* data, uint16_t bitIndex, bool value) {
  if (!value) {
    return;
  }

  const uint8_t byteIndex = static_cast<uint8_t>(bitIndex / 8U);
  const uint8_t bitInByte = static_cast<uint8_t>(bitIndex % 8U);
  data[byteIndex] |= static_cast<uint8_t>(1U << bitInByte);
}

static void readS88Into(uint8_t* data, uint8_t byteCount) {
  for (uint8_t index = 0; index < byteCount; ++index) {
    data[index] = 0;
  }

  const uint16_t inputCount = inputCountForBytes(byteCount);

  digitalWrite(Config::S88_CLOCK_PIN, LOW);
  digitalWrite(Config::S88_LOAD_PIN, LOW);
  digitalWrite(Config::S88_RESET_PIN, LOW);
  waitControlPulse();

  digitalWrite(Config::S88_LOAD_PIN, HIGH);
  waitControlPulse();

  setClock(true);
  setClock(false);
  storeBit(data, 0, readDataPin());

  digitalWrite(Config::S88_RESET_PIN, HIGH);
  waitControlPulse();
  digitalWrite(Config::S88_RESET_PIN, LOW);
  waitControlPulse();
  digitalWrite(Config::S88_LOAD_PIN, LOW);
  waitControlPulse();

  for (uint16_t bitIndex = 1; bitIndex < inputCount; ++bitIndex) {
    setClock(true);
    setClock(false);
    storeBit(data, bitIndex, readDataPin());
  }

  digitalWrite(Config::S88_CLOCK_PIN, LOW);
  digitalWrite(Config::S88_LOAD_PIN, LOW);
  digitalWrite(Config::S88_RESET_PIN, LOW);
}

static void publishSnapshot(const uint8_t* data, uint8_t byteCount) {
  noInterrupts();

  for (uint8_t index = 0; index < Config::MAX_BYTE_COUNT; ++index) {
    s88Snapshot[index] = index < byteCount ? data[index] : 0;
  }

  interrupts();
}

static void readAndPublishS88() {
  const uint8_t byteCount = readActiveByteCount();
  uint8_t nextSnapshot[Config::MAX_BYTE_COUNT] = {};

  readS88Into(nextSnapshot, byteCount);
  publishSnapshot(nextSnapshot, byteCount);
}

static void applyByteCount(
    uint8_t byteCount,
    ByteCountSource source,
    bool printMessage) {
  if (byteCount < 1 || byteCount > Config::MAX_BYTE_COUNT) {
    return;
  }

  if (byteCount == readActiveByteCount()) {
    noInterrupts();
    activeByteCountSource = source;
    interrupts();

    if (printMessage) {
      Serial.print(F("CONFIG unchanged: bytes="));
      Serial.print(byteCount);
      Serial.print(F(" inputs="));
      Serial.print(inputCountForBytes(byteCount));
      Serial.print(F(" source="));
      Serial.println(byteCountSourceName(source));
    }
    return;
  }

  // Prepare a complete new snapshot before atomically exposing the new byte
  // count to I2C request ISR.
  uint8_t nextSnapshot[Config::MAX_BYTE_COUNT] = {};
  readS88Into(nextSnapshot, byteCount);

  noInterrupts();

  for (uint8_t index = 0; index < Config::MAX_BYTE_COUNT; ++index) {
    s88Snapshot[index] = index < byteCount ? nextSnapshot[index] : 0;
  }

  activeByteCountValue = byteCount;
  activeByteCountSource = source;

  interrupts();

  if (printMessage) {
    Serial.print(F("CONFIG applied: bytes="));
    Serial.print(byteCount);
    Serial.print(F(" inputs="));
    Serial.print(inputCountForBytes(byteCount));
    Serial.print(F(" source="));
    Serial.println(byteCountSourceName(source));
  }
}

static uint8_t infoChecksum(const uint8_t* packet) {
  uint8_t checksum = 0;

  for (uint8_t index = 0; index < Config::INFO_RESPONSE_SIZE - 1; ++index) {
    checksum ^= packet[index];
  }

  return checksum;
}

static void writeInfoResponse() {
  uint8_t response[Config::INFO_RESPONSE_SIZE] = {};

  response[0] = Config::PROTOCOL_MAGIC;
  response[1] = Config::INFO_RESPONSE;
  response[2] = Config::PROTOCOL_VERSION;
  response[3] = S88_ADAPTER_VERSION_MAJOR;
  response[4] = S88_ADAPTER_VERSION_MINOR;
  response[5] = S88_ADAPTER_VERSION_PATCH;
  response[6] = readActiveByteCount();
  response[7] = Config::MAX_BYTE_COUNT;
  response[8] = Config::CAPABILITY_FLAGS;
  response[9] = infoChecksum(response);

  Wire.write(response, sizeof(response));
}

static void writeSnapshotResponse() {
  const uint8_t byteCount = readActiveByteCount();
  uint8_t response[Config::MAX_BYTE_COUNT] = {};

  for (uint8_t index = 0; index < byteCount; ++index) {
    response[index] = s88Snapshot[index];
  }

  Wire.write(response, byteCount);
}

// I2C read: return either the one-shot INFO packet or the normal S88 snapshot.
static void onI2CRequest() {
  const I2CResponseMode mode = nextI2CResponse;
  nextI2CResponse = I2CResponseMode::Snapshot;

  if (mode == I2CResponseMode::Info) {
    writeInfoResponse();
    return;
  }

  writeSnapshotResponse();
}

// I2C writes are intentionally read-selection only. They NEVER change address,
// byte count or any other adapter configuration.
static void onI2CReceive(int receivedCount) {
  uint8_t packet[3] = {};
  uint8_t index = 0;

  while (Wire.available() && index < sizeof(packet)) {
    packet[index++] = static_cast<uint8_t>(Wire.read());
  }

  while (Wire.available()) {
    Wire.read();
  }

  if (receivedCount != 3 || index != 3) {
    return;
  }

  const uint8_t checksum = static_cast<uint8_t>(packet[0] ^ packet[1]);

  if (
      packet[0] == Config::PROTOCOL_MAGIC &&
      packet[1] == Config::INFO_REQUEST &&
      packet[2] == checksum) {
    nextI2CResponse = I2CResponseMode::Info;
  }
}

static void printByteBinary(uint8_t value) {
  Serial.print('_');

  for (int8_t bit = 7; bit >= 0; --bit) {
    Serial.print((value & (1U << bit)) ? '1' : '0');
  }
}

static void printS88Data() {
  const uint8_t byteCount = readActiveByteCount();
  uint8_t snapshot[Config::MAX_BYTE_COUNT] = {};

  noInterrupts();
  for (uint8_t index = 0; index < byteCount; ++index) {
    snapshot[index] = s88Snapshot[index];
  }
  interrupts();

  Serial.print(F("S88:"));

  for (uint8_t index = 0; index < byteCount; ++index) {
    Serial.print(' ');
    printByteBinary(snapshot[index]);
  }

  Serial.println();
}

static void printHexAddress(uint8_t address) {
  Serial.print(F("0x"));
  if (address < 0x10) {
    Serial.print('0');
  }
  Serial.print(address, HEX);
}

static uint8_t eepromChecksum(uint8_t address, uint8_t byteCount) {
  return static_cast<uint8_t>(
      Config::EEPROM_MAGIC_0 ^
      Config::EEPROM_MAGIC_1 ^
      Config::EEPROM_VERSION ^
      address ^
      byteCount ^
      0xA5U);
}

static bool validI2CAddress(uint8_t address) {
  return address >= 0x08 && address <= 0x77;
}

static bool loadEepromConfiguration() {
  const uint8_t magic0 = EEPROM.read(Config::EEPROM_BASE + 0);
  const uint8_t magic1 = EEPROM.read(Config::EEPROM_BASE + 1);
  const uint8_t version = EEPROM.read(Config::EEPROM_BASE + 2);
  const uint8_t address = EEPROM.read(Config::EEPROM_BASE + 3);
  const uint8_t byteCount = EEPROM.read(Config::EEPROM_BASE + 4);
  const uint8_t checksum = EEPROM.read(Config::EEPROM_BASE + 5);

  const bool valid =
      magic0 == Config::EEPROM_MAGIC_0 &&
      magic1 == Config::EEPROM_MAGIC_1 &&
      version == Config::EEPROM_VERSION &&
      validI2CAddress(address) &&
      byteCount >= 1 &&
      byteCount <= Config::MAX_BYTE_COUNT &&
      checksum == eepromChecksum(address, byteCount);

  if (!valid) {
    activeI2CAddress = Config::DEFAULT_I2C_ADDRESS;
    stagedI2CAddress = Config::DEFAULT_I2C_ADDRESS;
    stagedByteCount = Config::DEFAULT_BYTE_COUNT;
    savedI2CAddress = Config::DEFAULT_I2C_ADDRESS;
    savedByteCount = Config::DEFAULT_BYTE_COUNT;
    eepromConfigurationValid = false;

    noInterrupts();
    activeByteCountValue = Config::DEFAULT_BYTE_COUNT;
    activeByteCountSource = ByteCountSource::FirmwareDefault;
    interrupts();

    return false;
  }

  activeI2CAddress = address;
  stagedI2CAddress = address;
  stagedByteCount = byteCount;
  savedI2CAddress = address;
  savedByteCount = byteCount;
  eepromConfigurationValid = true;

  noInterrupts();
  activeByteCountValue = byteCount;
  activeByteCountSource = ByteCountSource::Eeprom;
  interrupts();

  return true;
}

static void saveEepromConfiguration() {
  EEPROM.update(Config::EEPROM_BASE + 0, Config::EEPROM_MAGIC_0);
  EEPROM.update(Config::EEPROM_BASE + 1, Config::EEPROM_MAGIC_1);
  EEPROM.update(Config::EEPROM_BASE + 2, Config::EEPROM_VERSION);
  EEPROM.update(Config::EEPROM_BASE + 3, stagedI2CAddress);
  EEPROM.update(Config::EEPROM_BASE + 4, stagedByteCount);
  EEPROM.update(
      Config::EEPROM_BASE + 5,
      eepromChecksum(stagedI2CAddress, stagedByteCount));

  savedI2CAddress = stagedI2CAddress;
  savedByteCount = stagedByteCount;
  eepromConfigurationValid = true;
}

static bool configurationDirty() {
  return
      !eepromConfigurationValid ||
      stagedI2CAddress != savedI2CAddress ||
      stagedByteCount != savedByteCount;
}

static bool addressRestartRequired() {
  return stagedI2CAddress != activeI2CAddress;
}

static void printStatus() {
  const uint8_t byteCount = readActiveByteCount();
  const ByteCountSource source = readActiveByteCountSource();

  Serial.println();
  Serial.println(F("--- S88Adapter STATUS ---"));
  Serial.print(F("Firmware: v"));
  Serial.println(F(S88_ADAPTER_VERSION));
  Serial.print(F("Protocol: "));
  Serial.println(Config::PROTOCOL_VERSION);
  Serial.print(F("Uptime: "));
  Serial.print(millis() / 1000UL);
  Serial.println(F(" s"));

  Serial.print(F("I2C active address: "));
  printHexAddress(activeI2CAddress);
  Serial.println();

  Serial.print(F("I2C staged address: "));
  printHexAddress(stagedI2CAddress);
  if (addressRestartRequired()) {
    Serial.print(F("  [REBOOT REQUIRED]"));
  }
  Serial.println();

  Serial.print(F("I2C saved address: "));
  printHexAddress(savedI2CAddress);
  Serial.println();

  Serial.print(F("Active S88 bytes: "));
  Serial.print(byteCount);
  Serial.print(F("  inputs: "));
  Serial.print(inputCountForBytes(byteCount));
  Serial.print(F("  source: "));
  Serial.println(byteCountSourceName(source));

  Serial.print(F("Serial staged bytes: "));
  Serial.print(stagedByteCount);
  Serial.print(F("  inputs: "));
  Serial.println(inputCountForBytes(stagedByteCount));

  Serial.print(F("EEPROM saved bytes: "));
  Serial.println(savedByteCount);

  Serial.print(F("EEPROM valid: "));
  Serial.println(eepromConfigurationValid ? F("YES") : F("NO - firmware defaults in use"));

  Serial.print(F("Unsaved serial changes: "));
  Serial.println(configurationDirty() ? F("YES") : F("NO"));

  Serial.print(F("Periodic S88 log: "));
  Serial.println(periodicLoggingEnabled ? F("ON") : F("OFF"));

  Serial.println(F("Hub access: READ ONLY (INFO + snapshot)"));
  Serial.println(F("-------------------------"));
  Serial.println();
}

static void printInfo() {
  Serial.println();
  Serial.print(F("DCCExpress S88Adapter v"));
  Serial.println(F(S88_ADAPTER_VERSION));
  Serial.println(F("S88 -> USB Serial configured I2C slave"));
  Serial.print(F("I2C protocol version: "));
  Serial.println(Config::PROTOCOL_VERSION);
  Serial.println(F("Hub permissions: INFO + snapshot READ ONLY"));
  Serial.println(F("Transport: 1 byte = 8 S88 feedback inputs"));
  Serial.print(F("Maximum: "));
  Serial.print(Config::MAX_BYTE_COUNT);
  Serial.print(F(" bytes = "));
  Serial.print(inputCountForBytes(Config::MAX_BYTE_COUNT));
  Serial.println(F(" inputs"));
  Serial.println(F("UNO I2C: SDA=A4 SCL=A5"));
  Serial.println(F("S88 pins: CLOCK=D2 LOAD=D3 RESET=D4 DATA=D5"));

  const uint32_t clockHz =
      1000000UL /
      (static_cast<uint32_t>(Config::HALF_CLOCK_US) * 2UL);

  Serial.print(F("S88 clock: "));
  Serial.print(clockHz);
  Serial.println(F(" Hz"));
  Serial.println(F("I2C address and byte length are changed only from this serial console."));
  Serial.println(F("Serial SET values are stored only after SAVE."));
  Serial.println(F("Changing I2C address requires SAVE + REBOOT."));
  Serial.println();
}

static void printHelp() {
  Serial.println();
  Serial.println(F("--- S88Adapter serial commands ---"));
  Serial.println(F("HELP                         Show this help"));
  Serial.println(F("STATUS                       Show live/staged/saved configuration"));
  Serial.println(F("INFO                         Show firmware/protocol/hardware information"));
  Serial.println(F("READ                         Print current S88 snapshot now"));
  Serial.println(F("SET ADDRESS 0x30             Stage I2C slave address (0x08..0x77)"));
  Serial.println(F("SET I2C 0x30                 Alias for SET ADDRESS"));
  Serial.println(F("SET BYTES 4                  Set active/staged S88 length (1..32 bytes)"));
  Serial.println(F("SET GROUPS 4                 Alias for SET BYTES"));
  Serial.println(F("SAVE                         Save staged address + byte length to EEPROM"));
  Serial.println(F("DEFAULTS                     Stage firmware defaults; does not SAVE"));
  Serial.println(F("LOG ON                       Enable periodic S88 lines"));
  Serial.println(F("LOG OFF                      Disable periodic S88 lines"));
  Serial.println(F("REBOOT                       Reboot UNO; activates saved I2C address"));
  Serial.println(F("----------------------------------"));
  Serial.println();
}

static bool equalsIgnoreCase(const char* left, const char* right) {
  if (!left || !right) {
    return false;
  }

  while (*left && *right) {
    if (
        toupper(static_cast<unsigned char>(*left)) !=
        toupper(static_cast<unsigned char>(*right))) {
      return false;
    }
    ++left;
    ++right;
  }

  return *left == '\0' && *right == '\0';
}

static bool parseUnsigned(const char* text, unsigned long& value) {
  if (!text || !*text) {
    return false;
  }

  char* end = nullptr;
  const int base =
      (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
          ? 16
          : 10;

  value = strtoul(text, &end, base);
  return end && *end == '\0';
}

static void rebootDevice() {
  Serial.println(F("Rebooting..."));
  Serial.flush();
  delay(20);
  wdt_enable(WDTO_15MS);
  while (true) {
  }
}

static void handleSetCommand(char* field, char* valueText) {
  if (!field || !valueText) {
    Serial.println(F("ERR: usage SET ADDRESS <0x08..0x77> or SET BYTES <1..32>"));
    return;
  }

  unsigned long value = 0;
  if (!parseUnsigned(valueText, value)) {
    Serial.println(F("ERR: invalid numeric value"));
    return;
  }

  if (equalsIgnoreCase(field, "ADDRESS") || equalsIgnoreCase(field, "I2C")) {
    if (value < 0x08UL || value > 0x77UL) {
      Serial.println(F("ERR: I2C address must be 0x08..0x77"));
      return;
    }

    stagedI2CAddress = static_cast<uint8_t>(value);
    Serial.print(F("OK: staged I2C address="));
    printHexAddress(stagedI2CAddress);

    if (addressRestartRequired()) {
      Serial.print(F("; SAVE + REBOOT required to activate"));
    }
    Serial.println();
    return;
  }

  if (equalsIgnoreCase(field, "BYTES") || equalsIgnoreCase(field, "GROUPS")) {
    if (value < 1UL || value > Config::MAX_BYTE_COUNT) {
      Serial.print(F("ERR: byte count must be 1.."));
      Serial.println(Config::MAX_BYTE_COUNT);
      return;
    }

    stagedByteCount = static_cast<uint8_t>(value);
    applyByteCount(stagedByteCount, ByteCountSource::Serial, true);
    Serial.println(F("NOTE: SAVE stores this byte length for the next boot."));
    return;
  }

  Serial.println(F("ERR: unknown SET field; use ADDRESS/I2C or BYTES/GROUPS"));
}

static void handleSerialCommand(char* line) {
  char* command = strtok(line, " \t");
  if (!command) {
    return;
  }

  if (equalsIgnoreCase(command, "HELP") || equalsIgnoreCase(command, "?")) {
    printHelp();
    return;
  }

  if (equalsIgnoreCase(command, "STATUS") || equalsIgnoreCase(command, "SHOW")) {
    printStatus();
    return;
  }

  if (equalsIgnoreCase(command, "INFO")) {
    printInfo();
    return;
  }

  if (equalsIgnoreCase(command, "READ")) {
    printS88Data();
    return;
  }

  if (equalsIgnoreCase(command, "SET")) {
    char* field = strtok(nullptr, " \t");
    char* valueText = strtok(nullptr, " \t");
    handleSetCommand(field, valueText);
    return;
  }

  if (equalsIgnoreCase(command, "SAVE")) {
    saveEepromConfiguration();
    Serial.println(F("OK: configuration saved to EEPROM"));
    if (addressRestartRequired()) {
      Serial.println(F("NOTE: REBOOT required to activate the saved I2C address."));
    }
    return;
  }

  if (equalsIgnoreCase(command, "DEFAULTS")) {
    stagedI2CAddress = Config::DEFAULT_I2C_ADDRESS;
    stagedByteCount = Config::DEFAULT_BYTE_COUNT;
    applyByteCount(stagedByteCount, ByteCountSource::Serial, true);
    Serial.println(F("OK: firmware defaults staged; use SAVE to persist them."));
    if (addressRestartRequired()) {
      Serial.println(F("NOTE: SAVE + REBOOT required to activate the default I2C address."));
    }
    return;
  }

  if (equalsIgnoreCase(command, "LOG")) {
    char* value = strtok(nullptr, " \t");
    if (value && equalsIgnoreCase(value, "ON")) {
      periodicLoggingEnabled = true;
      Serial.println(F("OK: periodic S88 log ON"));
    } else if (value && equalsIgnoreCase(value, "OFF")) {
      periodicLoggingEnabled = false;
      Serial.println(F("OK: periodic S88 log OFF"));
    } else {
      Serial.println(F("ERR: usage LOG ON or LOG OFF"));
    }
    return;
  }

  if (equalsIgnoreCase(command, "REBOOT") || equalsIgnoreCase(command, "RESET")) {
    rebootDevice();
    return;
  }

  Serial.print(F("ERR: unknown command: "));
  Serial.println(command);
  Serial.println(F("Type HELP for available commands."));
}

static void processSerialInput() {
  while (Serial.available()) {
    const char ch = static_cast<char>(Serial.read());

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      serialLine[serialLineLength] = '\0';
      if (serialLineLength > 0) {
        handleSerialCommand(serialLine);
      }
      serialLineLength = 0;
      serialLine[0] = '\0';
      continue;
    }

    if (serialLineLength + 1 >= Config::SERIAL_LINE_SIZE) {
      serialLineLength = 0;
      serialLine[0] = '\0';
      Serial.println(F("ERR: command line too long"));
      continue;
    }

    serialLine[serialLineLength++] = ch;
  }
}

void setup() {
  // Recover cleanly if the previous reboot was watchdog-triggered.
  MCUSR = 0;
  wdt_disable();

  pinMode(Config::S88_CLOCK_PIN, OUTPUT);
  pinMode(Config::S88_LOAD_PIN, OUTPUT);
  pinMode(Config::S88_RESET_PIN, OUTPUT);
  pinMode(Config::S88_DATA_PIN, INPUT);

  digitalWrite(Config::S88_CLOCK_PIN, LOW);
  digitalWrite(Config::S88_LOAD_PIN, LOW);
  digitalWrite(Config::S88_RESET_PIN, LOW);

  Serial.begin(Config::SERIAL_BAUD);
  delay(500);

  const bool loadedFromEeprom = loadEepromConfiguration();

  readAndPublishS88();

  Wire.begin(activeI2CAddress);
  Wire.onRequest(onI2CRequest);
  Wire.onReceive(onI2CReceive);

  Serial.println();
  Serial.print(F("DCCExpress S88Adapter v"));
  Serial.println(F(S88_ADAPTER_VERSION));
  Serial.println(F("Mode: serial-configured S88 -> read-only I2C adapter"));

  Serial.print(F("Boot config: "));
  Serial.println(loadedFromEeprom ? F("EEPROM") : F("firmware defaults"));

  Serial.print(F("I2C slave address: "));
  printHexAddress(activeI2CAddress);
  Serial.println(F("  SDA=A4 SCL=A5"));

  Serial.print(F("S88 bytes: "));
  Serial.print(readActiveByteCount());
  Serial.print(F("  inputs: "));
  Serial.println(activeInputCount());

  Serial.print(F("I2C INFO protocol: v"));
  Serial.println(Config::PROTOCOL_VERSION);
  Serial.println(F("Hub can read INFO + snapshot; Hub cannot change adapter config."));
  Serial.println(F("Pins: CLOCK=D2 LOAD=D3 RESET=D4 DATA=D5"));

  const uint32_t clockHz =
      1000000UL /
      (static_cast<uint32_t>(Config::HALF_CLOCK_US) * 2UL);

  Serial.print(F("S88 clock: "));
  Serial.print(clockHz);
  Serial.println(F(" Hz"));
  Serial.println(F("Type HELP for serial configuration commands."));
  Serial.println();

  lastReadMs = millis();
  lastLogMs = millis();
}

void loop() {
  processSerialInput();

  const uint32_t now = millis();

  if (now - lastReadMs >= Config::READ_INTERVAL_MS) {
    lastReadMs = now;
    readAndPublishS88();
  }

  if (
      periodicLoggingEnabled &&
      now - lastLogMs >= Config::LOG_INTERVAL_MS) {
    lastLogMs = now;
    printS88Data();
  }
}

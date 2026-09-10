#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <avr/wdt.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "S88AdapterConfig.h"

// -----------------------------------------------------------------------------
// DCCExpress S88Adapter - v0.4.0
//
// Runtime configuration sources:
//   1. EEPROM boot defaults configured from USB serial.
//   2. Hub -> UNO I2C write can override the active group/byte count.
//
// Hub I2C configuration packet:
//   byte 0: 0xA5 magic
//   byte 1: 0x01 CONFIG
//   byte 2: group count
//   byte 3: byte count (= group count)
//   byte 4: XOR checksum of bytes 0..3
//
// One transport group = 8 S88 inputs = 1 byte.
// Maximum: 32 groups = 256 sensors = 32 bytes.
// -----------------------------------------------------------------------------

namespace Config {

constexpr uint32_t SERIAL_BAUD = 115200;

constexpr uint8_t S88_CLOCK_PIN = 2;
constexpr uint8_t S88_LOAD_PIN = 3;
constexpr uint8_t S88_RESET_PIN = 4;
constexpr uint8_t S88_DATA_PIN = 5;

constexpr uint8_t DEFAULT_I2C_ADDRESS = S88_I2C_ADDRESS;
constexpr uint8_t DEFAULT_GROUP_COUNT = S88_DEFAULT_GROUP_COUNT;
constexpr uint8_t MAX_GROUP_COUNT = S88_MAX_GROUP_COUNT;
constexpr uint8_t BYTES_PER_GROUP = 1;
constexpr uint8_t MAX_BYTE_COUNT = MAX_GROUP_COUNT * BYTES_PER_GROUP;

constexpr uint16_t HALF_CLOCK_US = S88_HALF_CLOCK_US;
constexpr uint16_t CONTROL_PULSE_US = S88_CONTROL_PULSE_US;
constexpr uint32_t READ_INTERVAL_MS = S88_READ_INTERVAL_MS;
constexpr uint32_t LOG_INTERVAL_MS = S88_LOG_INTERVAL_MS;

constexpr uint8_t CONFIG_MAGIC = 0xA5;
constexpr uint8_t CONFIG_COMMAND = 0x01;

constexpr uint8_t EEPROM_MAGIC_0 = 0xD8;
constexpr uint8_t EEPROM_MAGIC_1 = 0x53;
constexpr uint8_t EEPROM_VERSION = 1;
constexpr int EEPROM_BASE = 0;
constexpr uint8_t EEPROM_SIZE = 6;

constexpr size_t SERIAL_LINE_SIZE = 64;

} // namespace Config

enum class GroupSource : uint8_t {
  FirmwareDefault,
  Eeprom,
  Serial,
  Hub
};

static volatile uint8_t s88Snapshot[Config::MAX_BYTE_COUNT] = {};

static volatile uint8_t activeGroupCount = Config::DEFAULT_GROUP_COUNT;
static volatile GroupSource activeGroupSource = GroupSource::FirmwareDefault;

static volatile bool configPending = false;
static volatile uint8_t pendingGroupCount = Config::DEFAULT_GROUP_COUNT;

static uint8_t activeI2CAddress = Config::DEFAULT_I2C_ADDRESS;

// Serial-editable boot configuration. SET changes these RAM values, SAVE writes
// them to EEPROM. The active group count may later be overridden by the Hub.
static uint8_t stagedI2CAddress = Config::DEFAULT_I2C_ADDRESS;
static uint8_t stagedGroupCount = Config::DEFAULT_GROUP_COUNT;

static uint8_t savedI2CAddress = Config::DEFAULT_I2C_ADDRESS;
static uint8_t savedGroupCount = Config::DEFAULT_GROUP_COUNT;
static bool eepromConfigurationValid = false;

static bool periodicLoggingEnabled = true;

static uint32_t lastReadMs = 0;
static uint32_t lastLogMs = 0;

static char serialLine[Config::SERIAL_LINE_SIZE] = {};
static size_t serialLineLength = 0;

static inline uint8_t byteCountForGroups(uint8_t groups) {
  return static_cast<uint8_t>(groups * Config::BYTES_PER_GROUP);
}

static inline uint16_t inputCountForGroups(uint8_t groups) {
  return static_cast<uint16_t>(groups) * 8U;
}

static uint8_t readActiveGroupCount() {
  // uint8_t reads are atomic on ATmega328P. Avoid toggling the global interrupt
  // state here because this helper is also used from the Wire request ISR.
  return activeGroupCount;
}

static GroupSource readActiveGroupSource() {
  // GroupSource uses an 8-bit underlying type, so this read is atomic too.
  return activeGroupSource;
}

static inline uint8_t activeByteCount() {
  return byteCountForGroups(readActiveGroupCount());
}

static inline uint16_t activeInputCount() {
  return inputCountForGroups(readActiveGroupCount());
}

static const __FlashStringHelper* groupSourceName(GroupSource source) {
  switch (source) {
    case GroupSource::Eeprom:
      return F("EEPROM");
    case GroupSource::Serial:
      return F("SERIAL");
    case GroupSource::Hub:
      return F("HUB");
    case GroupSource::FirmwareDefault:
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

  const uint16_t inputCount = static_cast<uint16_t>(byteCount) * 8U;

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

static void commitSnapshotAndGroupCount(
    const uint8_t* data,
    uint8_t byteCount,
    uint8_t groups,
    GroupSource source) {
  noInterrupts();

  for (uint8_t index = 0; index < Config::MAX_BYTE_COUNT; ++index) {
    s88Snapshot[index] = index < byteCount ? data[index] : 0;
  }

  activeGroupCount = groups;
  activeGroupSource = source;

  interrupts();
}

static void publishSnapshot(const uint8_t* data, uint8_t byteCount) {
  noInterrupts();

  for (uint8_t index = 0; index < Config::MAX_BYTE_COUNT; ++index) {
    s88Snapshot[index] = index < byteCount ? data[index] : 0;
  }

  interrupts();
}

static void readAndPublishS88() {
  const uint8_t byteCount = activeByteCount();
  uint8_t nextSnapshot[Config::MAX_BYTE_COUNT] = {};

  readS88Into(nextSnapshot, byteCount);
  publishSnapshot(nextSnapshot, byteCount);
}

static void applyGroupCount(uint8_t groups, GroupSource source, bool printMessage) {
  if (groups < 1 || groups > Config::MAX_GROUP_COUNT) {
    return;
  }

  const uint8_t currentGroups = readActiveGroupCount();

  if (groups == currentGroups) {
    noInterrupts();
    activeGroupSource = source;
    interrupts();

    if (printMessage) {
      Serial.print(F("CONFIG unchanged: bytes="));
      Serial.print(groups);
      Serial.print(F(" inputs="));
      Serial.print(inputCountForGroups(groups));
      Serial.print(F(" source="));
      Serial.println(groupSourceName(source));
    }
    return;
  }

  const uint8_t byteCount = byteCountForGroups(groups);

  // Build the new snapshot while the old group count remains visible to the
  // I2C request ISR. Commit snapshot + group count atomically afterwards.
  uint8_t nextSnapshot[Config::MAX_BYTE_COUNT] = {};
  readS88Into(nextSnapshot, byteCount);
  commitSnapshotAndGroupCount(nextSnapshot, byteCount, groups, source);

  if (printMessage) {
    Serial.print(F("CONFIG applied: bytes="));
    Serial.print(groups);
    Serial.print(F(" inputs="));
    Serial.print(inputCountForGroups(groups));
    Serial.print(F(" source="));
    Serial.println(groupSourceName(source));
  }
}

// I2C master requests the current raw S88 snapshot.
static void onI2CRequest() {
  const uint8_t byteCount = activeByteCount();
  uint8_t response[Config::MAX_BYTE_COUNT];

  for (uint8_t index = 0; index < byteCount; ++index) {
    response[index] = s88Snapshot[index];
  }

  Wire.write(response, byteCount);
}

// Hub sends runtime group/byte configuration here.
// Keep this ISR callback tiny: validate and publish a pending group count only.
static void onI2CReceive(int receivedCount) {
  uint8_t packet[5] = {};
  uint8_t index = 0;

  while (Wire.available() && index < sizeof(packet)) {
    packet[index++] = static_cast<uint8_t>(Wire.read());
  }

  while (Wire.available()) {
    Wire.read();
  }

  if (receivedCount != 5 || index != 5) {
    return;
  }

  const uint8_t checksum = static_cast<uint8_t>(
      packet[0] ^ packet[1] ^ packet[2] ^ packet[3]);

  if (
      packet[0] != Config::CONFIG_MAGIC ||
      packet[1] != Config::CONFIG_COMMAND ||
      packet[4] != checksum) {
    return;
  }

  const uint8_t groups = packet[2];
  const uint8_t bytes = packet[3];

  if (
      groups < 1 ||
      groups > Config::MAX_GROUP_COUNT ||
      bytes != byteCountForGroups(groups)) {
    return;
  }

  if (groups == activeGroupCount) {
    activeGroupSource = GroupSource::Hub;
    return;
  }

  pendingGroupCount = groups;
  configPending = true;
}

static void applyPendingConfiguration() {
  if (!configPending) {
    return;
  }

  noInterrupts();
  const uint8_t groups = pendingGroupCount;
  configPending = false;
  interrupts();

  applyGroupCount(groups, GroupSource::Hub, true);
}

static void printByteBinary(uint8_t value) {
  Serial.print('_');

  for (int8_t bit = 7; bit >= 0; --bit) {
    Serial.print((value & (1U << bit)) ? '1' : '0');
  }
}

static void printS88Data() {
  const uint8_t byteCount = activeByteCount();
  uint8_t snapshot[Config::MAX_BYTE_COUNT];

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

static uint8_t eepromChecksum(
    uint8_t address,
    uint8_t groups) {
  return static_cast<uint8_t>(
      Config::EEPROM_MAGIC_0 ^
      Config::EEPROM_MAGIC_1 ^
      Config::EEPROM_VERSION ^
      address ^
      groups ^
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
  const uint8_t groups = EEPROM.read(Config::EEPROM_BASE + 4);
  const uint8_t checksum = EEPROM.read(Config::EEPROM_BASE + 5);

  const bool valid =
      magic0 == Config::EEPROM_MAGIC_0 &&
      magic1 == Config::EEPROM_MAGIC_1 &&
      version == Config::EEPROM_VERSION &&
      validI2CAddress(address) &&
      groups >= 1 &&
      groups <= Config::MAX_GROUP_COUNT &&
      checksum == eepromChecksum(address, groups);

  if (!valid) {
    activeI2CAddress = Config::DEFAULT_I2C_ADDRESS;
    stagedI2CAddress = Config::DEFAULT_I2C_ADDRESS;
    stagedGroupCount = Config::DEFAULT_GROUP_COUNT;
    savedI2CAddress = Config::DEFAULT_I2C_ADDRESS;
    savedGroupCount = Config::DEFAULT_GROUP_COUNT;
    eepromConfigurationValid = false;

    noInterrupts();
    activeGroupCount = Config::DEFAULT_GROUP_COUNT;
    activeGroupSource = GroupSource::FirmwareDefault;
    interrupts();

    return false;
  }

  activeI2CAddress = address;
  stagedI2CAddress = address;
  stagedGroupCount = groups;
  savedI2CAddress = address;
  savedGroupCount = groups;
  eepromConfigurationValid = true;

  noInterrupts();
  activeGroupCount = groups;
  activeGroupSource = GroupSource::Eeprom;
  interrupts();

  return true;
}

static void saveEepromConfiguration() {
  EEPROM.update(Config::EEPROM_BASE + 0, Config::EEPROM_MAGIC_0);
  EEPROM.update(Config::EEPROM_BASE + 1, Config::EEPROM_MAGIC_1);
  EEPROM.update(Config::EEPROM_BASE + 2, Config::EEPROM_VERSION);
  EEPROM.update(Config::EEPROM_BASE + 3, stagedI2CAddress);
  EEPROM.update(Config::EEPROM_BASE + 4, stagedGroupCount);
  EEPROM.update(
      Config::EEPROM_BASE + 5,
      eepromChecksum(stagedI2CAddress, stagedGroupCount));

  savedI2CAddress = stagedI2CAddress;
  savedGroupCount = stagedGroupCount;
  eepromConfigurationValid = true;
}

static bool configurationDirty() {
  return
      !eepromConfigurationValid ||
      stagedI2CAddress != savedI2CAddress ||
      stagedGroupCount != savedGroupCount;
}

static bool addressRestartRequired() {
  return stagedI2CAddress != activeI2CAddress;
}

static void printStatus() {
  const uint8_t groups = readActiveGroupCount();
  const GroupSource source = readActiveGroupSource();

  Serial.println();
  Serial.println(F("--- S88Adapter STATUS ---"));
  Serial.print(F("Firmware: v"));
  Serial.println(F(S88_ADAPTER_VERSION));
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

  Serial.print(F("Active bytes/groups: "));
  Serial.print(groups);
  Serial.print(F("  inputs: "));
  Serial.print(inputCountForGroups(groups));
  Serial.print(F("  source: "));
  Serial.println(groupSourceName(source));

  Serial.print(F("Serial staged bytes: "));
  Serial.print(stagedGroupCount);
  Serial.print(F("  inputs: "));
  Serial.println(inputCountForGroups(stagedGroupCount));

  Serial.print(F("EEPROM saved bytes: "));
  Serial.println(savedGroupCount);

  Serial.print(F("EEPROM valid: "));
  Serial.println(eepromConfigurationValid ? F("YES") : F("NO - firmware defaults in use"));

  Serial.print(F("Unsaved serial changes: "));
  Serial.println(configurationDirty() ? F("YES") : F("NO"));

  Serial.print(F("Periodic S88 log: "));
  Serial.println(periodicLoggingEnabled ? F("ON") : F("OFF"));

  Serial.println(F("-------------------------"));
  Serial.println();
}

static void printInfo() {
  Serial.println();
  Serial.print(F("DCCExpress S88Adapter v"));
  Serial.println(F(S88_ADAPTER_VERSION));
  Serial.println(F("S88 -> USB Serial + configurable I2C slave"));
  Serial.println(F("Transport: 1 group = 1 byte = 8 feedback inputs"));
  Serial.print(F("Maximum: "));
  Serial.print(Config::MAX_GROUP_COUNT);
  Serial.print(F(" bytes/groups = "));
  Serial.print(inputCountForGroups(Config::MAX_GROUP_COUNT));
  Serial.println(F(" inputs"));
  Serial.println(F("UNO I2C: SDA=A4 SCL=A5"));
  Serial.println(F("S88 pins: CLOCK=D2 LOAD=D3 RESET=D4 DATA=D5"));

  const uint32_t clockHz =
      1000000UL /
      (static_cast<uint32_t>(Config::HALF_CLOCK_US) * 2UL);

  Serial.print(F("S88 clock: "));
  Serial.print(clockHz);
  Serial.println(F(" Hz"));
  Serial.println(F("Hub may override active byte count at runtime over I2C."));
  Serial.println(F("Serial SET values are stored only after SAVE."));
  Serial.println(F("Changing I2C address requires SAVE + REBOOT."));
  Serial.println();
}

static void printHelp() {
  Serial.println();
  Serial.println(F("--- S88Adapter serial commands ---"));
  Serial.println(F("HELP                         Show this help"));
  Serial.println(F("STATUS                       Show live/staged/saved configuration"));
  Serial.println(F("INFO                         Show firmware and hardware information"));
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
    if (toupper(static_cast<unsigned char>(*left)) !=
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
    if (value < 1UL || value > Config::MAX_GROUP_COUNT) {
      Serial.print(F("ERR: byte/group count must be 1.."));
      Serial.println(Config::MAX_GROUP_COUNT);
      return;
    }

    stagedGroupCount = static_cast<uint8_t>(value);
    applyGroupCount(stagedGroupCount, GroupSource::Serial, true);
    Serial.println(F("NOTE: SAVE stores this as the next boot default; Hub may override it while connected."));
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
    stagedGroupCount = Config::DEFAULT_GROUP_COUNT;
    applyGroupCount(stagedGroupCount, GroupSource::Serial, true);
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
  Serial.println(F("Mode: S88 -> Serial + configurable I2C slave"));

  Serial.print(F("Boot config: "));
  Serial.println(loadedFromEeprom ? F("EEPROM") : F("firmware defaults"));

  Serial.print(F("I2C slave address: "));
  printHexAddress(activeI2CAddress);
  Serial.println(F("  SDA=A4 SCL=A5"));

  Serial.print(F("Active 8-bit groups/bytes: "));
  Serial.print(activeByteCount());
  Serial.print(F("  inputs: "));
  Serial.println(activeInputCount());

  Serial.println(F("Hub may change byte-group count at runtime over I2C."));
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
  applyPendingConfiguration();

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

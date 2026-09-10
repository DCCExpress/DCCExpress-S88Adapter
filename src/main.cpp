#include <Arduino.h>
#include <Wire.h>

#include "S88AdapterConfig.h"

// -----------------------------------------------------------------------------
// DCCExpress S88Adapter - v0.3.0
//
// Runtime configuration:
//   Hub -> UNO I2C write:
//     byte 0: 0xA5 magic
//     byte 1: 0x01 CONFIG
//     byte 2: group count
//     byte 3: byte count (= group count * 2)
//     byte 4: XOR checksum of bytes 0..3
//
// One transport group = 8 S88 inputs = 1 byte.
// Physical module boundaries do not matter.
// Maximum: 32 groups = 256 sensors = 32 bytes.
// -----------------------------------------------------------------------------

namespace Config {

constexpr uint32_t SERIAL_BAUD =
    115200;

constexpr uint8_t S88_CLOCK_PIN =
    2;

constexpr uint8_t S88_LOAD_PIN =
    3;

constexpr uint8_t S88_RESET_PIN =
    4;

constexpr uint8_t S88_DATA_PIN =
    5;

constexpr uint8_t I2C_ADDRESS =
    S88_I2C_ADDRESS;

constexpr uint8_t DEFAULT_GROUP_COUNT =
    S88_DEFAULT_GROUP_COUNT;

constexpr uint8_t MAX_GROUP_COUNT =
    S88_MAX_GROUP_COUNT;

constexpr uint8_t BYTES_PER_GROUP =
    1;

constexpr uint8_t MAX_BYTE_COUNT =
    MAX_GROUP_COUNT *
    BYTES_PER_GROUP;

constexpr uint16_t HALF_CLOCK_US =
    S88_HALF_CLOCK_US;

constexpr uint16_t CONTROL_PULSE_US =
    S88_CONTROL_PULSE_US;

constexpr uint32_t READ_INTERVAL_MS =
    S88_READ_INTERVAL_MS;

constexpr uint32_t LOG_INTERVAL_MS =
    S88_LOG_INTERVAL_MS;

constexpr uint8_t CONFIG_MAGIC =
    0xA5;

constexpr uint8_t CONFIG_COMMAND =
    0x01;

} // namespace Config

static volatile uint8_t
    s88Snapshot[
        Config::MAX_BYTE_COUNT] = {};

static volatile uint8_t
    activeGroupCount =
        Config::DEFAULT_GROUP_COUNT;

static volatile bool
    configPending =
        false;

static volatile uint8_t
    pendingGroupCount =
        Config::DEFAULT_GROUP_COUNT;

static uint32_t
    lastReadMs =
        0;

static uint32_t
    lastLogMs =
        0;

static inline uint8_t activeByteCount() {
    return
        static_cast<uint8_t>(
            activeGroupCount *
            Config::BYTES_PER_GROUP);
}

static inline uint16_t activeInputCount() {
    return
        static_cast<uint16_t>(
            activeGroupCount) *
        8U;
}

static inline void waitControlPulse() {
    delayMicroseconds(
        Config::CONTROL_PULSE_US);
}

static inline void setClock(
    bool high) {
    digitalWrite(
        Config::S88_CLOCK_PIN,
        high
            ? HIGH
            : LOW);

    delayMicroseconds(
        Config::HALF_CLOCK_US);
}

static inline bool readDataPin() {
    return
        digitalRead(
            Config::S88_DATA_PIN) ==
        HIGH;
}

static void storeBit(
    uint8_t* data,
    uint16_t bitIndex,
    bool value) {
    if (!value) {
        return;
    }

    const uint8_t byteIndex =
        static_cast<uint8_t>(
            bitIndex /
            8U);

    const uint8_t bitInByte =
        static_cast<uint8_t>(
            bitIndex %
            8U);

    data[byteIndex] |=
        static_cast<uint8_t>(
            1U <<
            bitInByte);
}

static void readS88Into(
    uint8_t* data,
    uint8_t byteCount) {
    for (
        uint8_t index = 0;
        index < byteCount;
        ++index
    ) {
        data[index] =
            0;
    }

    const uint16_t inputCount =
        static_cast<uint16_t>(
            byteCount) *
        8U;

    digitalWrite(
        Config::S88_CLOCK_PIN,
        LOW);

    digitalWrite(
        Config::S88_LOAD_PIN,
        LOW);

    digitalWrite(
        Config::S88_RESET_PIN,
        LOW);

    waitControlPulse();

    digitalWrite(
        Config::S88_LOAD_PIN,
        HIGH);

    waitControlPulse();

    setClock(
        true);

    setClock(
        false);

    storeBit(
        data,
        0,
        readDataPin());

    digitalWrite(
        Config::S88_RESET_PIN,
        HIGH);

    waitControlPulse();

    digitalWrite(
        Config::S88_RESET_PIN,
        LOW);

    waitControlPulse();

    digitalWrite(
        Config::S88_LOAD_PIN,
        LOW);

    waitControlPulse();

    for (
        uint16_t bitIndex = 1;
        bitIndex < inputCount;
        ++bitIndex
    ) {
        setClock(
            true);

        setClock(
            false);

        storeBit(
            data,
            bitIndex,
            readDataPin());
    }

    digitalWrite(
        Config::S88_CLOCK_PIN,
        LOW);

    digitalWrite(
        Config::S88_LOAD_PIN,
        LOW);

    digitalWrite(
        Config::S88_RESET_PIN,
        LOW);
}

static void publishSnapshot(
    const uint8_t* data,
    uint8_t byteCount) {
    noInterrupts();

    for (
        uint8_t index = 0;
        index <
            Config::MAX_BYTE_COUNT;
        ++index
    ) {
        s88Snapshot[index] =
            index <
                byteCount
                ? data[index]
                : 0;
    }

    interrupts();
}

static void readAndPublishS88() {
    const uint8_t byteCount =
        activeByteCount();

    uint8_t nextSnapshot[
        Config::MAX_BYTE_COUNT] = {};

    readS88Into(
        nextSnapshot,
        byteCount);

    publishSnapshot(
        nextSnapshot,
        byteCount);
}

// I2C master requests the current raw S88 snapshot.
static void onI2CRequest() {
    const uint8_t byteCount =
        activeByteCount();

    uint8_t response[
        Config::MAX_BYTE_COUNT];

    for (
        uint8_t index = 0;
        index < byteCount;
        ++index
    ) {
        response[index] =
            s88Snapshot[index];
    }

    Wire.write(
        response,
        byteCount);
}

// Hub sends runtime group/byte configuration here.
// Keep this ISR callback tiny: validate and publish a pending group count only.
static void onI2CReceive(
    int receivedCount) {
    uint8_t packet[5] = {};
    uint8_t index = 0;

    while (
        Wire.available() &&
        index <
            sizeof(packet)
    ) {
        packet[index++] =
            static_cast<uint8_t>(
                Wire.read());
    }

    while (
        Wire.available()
    ) {
        Wire.read();
    }

    if (
        receivedCount !=
            5 ||
        index !=
            5
    ) {
        return;
    }

    const uint8_t checksum =
        static_cast<uint8_t>(
            packet[0] ^
            packet[1] ^
            packet[2] ^
            packet[3]);

    if (
        packet[0] !=
            Config::CONFIG_MAGIC ||
        packet[1] !=
            Config::CONFIG_COMMAND ||
        packet[4] !=
            checksum
    ) {
        return;
    }

    const uint8_t groups =
        packet[2];

    const uint8_t bytes =
        packet[3];

    if (
        groups < 1 ||
        groups >
            Config::MAX_GROUP_COUNT ||
        bytes !=
            static_cast<uint8_t>(
                groups *
                Config::BYTES_PER_GROUP)
    ) {
        return;
    }

    pendingGroupCount =
        groups;

    configPending =
        true;
}

static void applyPendingConfiguration() {
    if (!configPending) {
        return;
    }

    noInterrupts();

    const uint8_t groups =
        pendingGroupCount;

    configPending =
        false;

    interrupts();

    activeGroupCount =
        groups;

    readAndPublishS88();

    Serial.print(
        F("I2C CONFIG applied: groups="));

    Serial.print(
        activeGroupCount);

    Serial.print(
        F(" bytes="));

    Serial.print(
        activeByteCount());

    Serial.print(
        F(" inputs="));

    Serial.println(
        activeInputCount());
}

static void printByteBinary(
    uint8_t value) {
    Serial.print(
        '_');

    for (
        int8_t bit = 7;
        bit >= 0;
        --bit
    ) {
        Serial.print(
            (
                value &
                (
                    1U <<
                    bit
                )
            )
                ? '1'
                : '0');
    }
}

static void printS88Data() {
    const uint8_t byteCount =
        activeByteCount();

    uint8_t snapshot[
        Config::MAX_BYTE_COUNT];

    noInterrupts();

    for (
        uint8_t index = 0;
        index < byteCount;
        ++index
    ) {
        snapshot[index] =
            s88Snapshot[index];
    }

    interrupts();

    Serial.print(
        F("S88:"));

    for (
        uint8_t index = 0;
        index < byteCount;
        ++index
    ) {
        Serial.print(
            ' ');

        printByteBinary(
            snapshot[index]);
    }

    Serial.println();
}

static void printHexAddress(
    uint8_t address) {
    Serial.print(
        F("0x"));

    if (
        address <
        0x10
    ) {
        Serial.print(
            '0');
    }

    Serial.print(
        address,
        HEX);
}

void setup() {
    pinMode(
        Config::S88_CLOCK_PIN,
        OUTPUT);

    pinMode(
        Config::S88_LOAD_PIN,
        OUTPUT);

    pinMode(
        Config::S88_RESET_PIN,
        OUTPUT);

    pinMode(
        Config::S88_DATA_PIN,
        INPUT);

    digitalWrite(
        Config::S88_CLOCK_PIN,
        LOW);

    digitalWrite(
        Config::S88_LOAD_PIN,
        LOW);

    digitalWrite(
        Config::S88_RESET_PIN,
        LOW);

    Serial.begin(
        Config::SERIAL_BAUD);

    delay(
        500);

    readAndPublishS88();

    Wire.begin(
        Config::I2C_ADDRESS);

    Wire.onRequest(
        onI2CRequest);

    Wire.onReceive(
        onI2CReceive);

    Serial.println();

    Serial.println(
        F("DCCExpress S88Adapter v0.3.0"));

    Serial.println(
        F("Mode: S88 -> Serial + configurable I2C slave"));

    Serial.print(
        F("I2C slave address: "));

    printHexAddress(
        Config::I2C_ADDRESS);

    Serial.println(
        F("  SDA=A4 SCL=A5"));

    Serial.print(
        F("Default 8-bit groups: "));

    Serial.print(
        activeGroupCount);

    Serial.print(
        F("  bytes: "));

    Serial.print(
        activeByteCount());

    Serial.print(
        F("  inputs: "));

    Serial.println(
        activeInputCount());

    Serial.println(
        F("Hub may change byte-group count at runtime over I2C."));

    Serial.println(
        F("Pins: CLOCK=D2 LOAD=D3 RESET=D4 DATA=D5"));

    Serial.print(
        F("S88 clock: "));

    const uint32_t clockHz =
        1000000UL /
        (
            static_cast<uint32_t>(
                Config::HALF_CLOCK_US) *
            2UL
        );

    Serial.print(
        clockHz);

    Serial.println(
        F(" Hz"));

    Serial.println();

    lastReadMs =
        millis();

    lastLogMs =
        millis();
}

void loop() {
    applyPendingConfiguration();

    const uint32_t now =
        millis();

    if (
        now -
            lastReadMs >=
        Config::READ_INTERVAL_MS
    ) {
        lastReadMs =
            now;

        readAndPublishS88();
    }

    if (
        now -
            lastLogMs >=
        Config::LOG_INTERVAL_MS
    ) {
        lastLogMs =
            now;

        printS88Data();
    }
}

#include <Arduino.h>
#include <Wire.h>

#include "S88AdapterConfig.h"

// -----------------------------------------------------------------------------
// DCCExpress S88Adapter - v0.2.0
//
// Arduino Uno:
//   - reads S88 / s88-N feedback data,
//   - keeps the latest snapshot in memory,
//   - exposes the snapshot as an I2C slave,
//   - keeps the USB serial logger for diagnostics.
//
// I2C:
//   SDA = A4
//   SCL = A5
// -----------------------------------------------------------------------------

namespace Config {

constexpr uint32_t SERIAL_BAUD =
    115200;

// Arduino Uno S88 pin assignment.
constexpr uint8_t S88_CLOCK_PIN =
    2;

constexpr uint8_t S88_LOAD_PIN =
    3; // PS / LOAD

constexpr uint8_t S88_RESET_PIN =
    4;

constexpr uint8_t S88_DATA_PIN =
    5;

constexpr uint8_t I2C_ADDRESS =
    S88_I2C_ADDRESS;

constexpr uint8_t MODULE_COUNT =
    S88_MODULE_COUNT;

constexpr uint16_t INPUT_COUNT =
    static_cast<uint16_t>(
        MODULE_COUNT) *
    16U;

constexpr uint8_t BYTE_COUNT =
    static_cast<uint8_t>(
        (INPUT_COUNT + 7U) /
        8U);

constexpr uint16_t HALF_CLOCK_US =
    S88_HALF_CLOCK_US;

constexpr uint16_t CONTROL_PULSE_US =
    S88_CONTROL_PULSE_US;

constexpr uint32_t READ_INTERVAL_MS =
    S88_READ_INTERVAL_MS;

constexpr uint32_t LOG_INTERVAL_MS =
    S88_LOG_INTERVAL_MS;

} // namespace Config

// Latest complete S88 snapshot exposed through I2C.
//
// The loop builds the next snapshot in a local buffer and copies it here
// atomically. The Wire onRequest callback therefore never sees a half-updated
// multi-byte snapshot.
static volatile uint8_t
    s88Snapshot[Config::BYTE_COUNT] = {};

static uint32_t
    lastReadMs = 0;

static uint32_t
    lastLogMs = 0;

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
    uint8_t* data) {
    for (
        uint8_t index = 0;
        index < Config::BYTE_COUNT;
        ++index
    ) {
        data[index] = 0;
    }

    // Defined S88 idle state.
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

    // LOAD / PS high, followed by the first clock pulse.
    digitalWrite(
        Config::S88_LOAD_PIN,
        HIGH);

    waitControlPulse();

    setClock(true);
    setClock(false);

    // First feedback bit is available after the first clock pulse.
    storeBit(
        data,
        0,
        readDataPin());

    // Reset the input latches while the loaded snapshot remains in the shift
    // registers.
    digitalWrite(
        Config::S88_RESET_PIN,
        HIGH);

    waitControlPulse();

    digitalWrite(
        Config::S88_RESET_PIN,
        LOW);

    waitControlPulse();

    // End LOAD phase and shift out the remaining bits.
    digitalWrite(
        Config::S88_LOAD_PIN,
        LOW);

    waitControlPulse();

    for (
        uint16_t bitIndex = 1;
        bitIndex < Config::INPUT_COUNT;
        ++bitIndex
    ) {
        setClock(true);
        setClock(false);

        storeBit(
            data,
            bitIndex,
            readDataPin());
    }

    // Return to idle.
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
    const uint8_t* data) {
    // Prevent the I2C request ISR from reading between individual byte copies.
    noInterrupts();

    for (
        uint8_t index = 0;
        index < Config::BYTE_COUNT;
        ++index
    ) {
        s88Snapshot[index] =
            data[index];
    }

    interrupts();
}

static void readAndPublishS88() {
    uint8_t nextSnapshot[
        Config::BYTE_COUNT] = {};

    readS88Into(
        nextSnapshot);

    publishSnapshot(
        nextSnapshot);
}

// Called by the Wire library when the I2C master requests data.
//
// Do not use Serial, delay(), or other slow operations here. This callback
// executes from the AVR TWI interrupt context.
static void onI2CRequest() {
    uint8_t response[
        Config::BYTE_COUNT];

    // We are already in interrupt context, so the main loop cannot modify
    // s88Snapshot while this copy is taking place.
    for (
        uint8_t index = 0;
        index < Config::BYTE_COUNT;
        ++index
    ) {
        response[index] =
            s88Snapshot[index];
    }

    Wire.write(
        response,
        Config::BYTE_COUNT);
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
    uint8_t snapshot[
        Config::BYTE_COUNT];

    noInterrupts();

    for (
        uint8_t index = 0;
        index < Config::BYTE_COUNT;
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
        index < Config::BYTE_COUNT;
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

    if (address < 0x10) {
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

    // Create the first valid S88 snapshot before exposing the I2C slave.
    readAndPublishS88();

    // Arduino Uno hardware I2C:
    //   SDA = A4
    //   SCL = A5
    Wire.begin(
        Config::I2C_ADDRESS);

    Wire.onRequest(
        onI2CRequest);

    Serial.println();
    Serial.println(
        F("DCCExpress S88Adapter v0.2.0"));

    Serial.println(
        F("Mode: S88 -> Serial + I2C slave"));

    Serial.print(
        F("I2C slave address: "));

    printHexAddress(
        Config::I2C_ADDRESS);

    Serial.println(
        F("  SDA=A4 SCL=A5"));

    Serial.print(
        F("S88 modules: "));

    Serial.print(
        Config::MODULE_COUNT);

    Serial.print(
        F("  inputs: "));

    Serial.print(
        Config::INPUT_COUNT);

    Serial.print(
        F("  bytes: "));

    Serial.println(
        Config::BYTE_COUNT);

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

    Serial.println(
        F("I2C byte 0 = inputs 1..8, byte 1 = inputs 9..16"));

    Serial.println(
        F("Example: 01 00 = input 1 occupied"));

    Serial.println();

    lastReadMs =
        millis();

    lastLogMs =
        millis();
}

void loop() {
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

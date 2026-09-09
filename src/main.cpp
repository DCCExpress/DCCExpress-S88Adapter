#include <Arduino.h>

// -----------------------------------------------------------------------------
// DCCExpress S88Adapter - v0.1.0
// First test version: read one S88 module (16 inputs) and log it to Serial.
// No I2C yet.
// -----------------------------------------------------------------------------

namespace Config {

constexpr uint32_t SERIAL_BAUD = 115200;

// Arduino Uno pin assignment.
constexpr uint8_t S88_CLOCK_PIN = 2;
constexpr uint8_t S88_LOAD_PIN  = 3; // PS / LOAD
constexpr uint8_t S88_RESET_PIN = 4;
constexpr uint8_t S88_DATA_PIN  = 5;

// One traditional S88 module contains 16 feedback inputs.
constexpr uint8_t S88_MODULE_COUNT = 1;
constexpr uint16_t S88_INPUT_COUNT = S88_MODULE_COUNT * 16;
constexpr uint8_t S88_BYTE_COUNT = S88_INPUT_COUNT / 8;

// Conservative timing for the first hardware test.
// 50 us HIGH + 50 us LOW = 10 kHz clock.
constexpr uint16_t S88_HALF_CLOCK_US = 50;
constexpr uint16_t S88_CONTROL_PULSE_US = 50;

constexpr uint32_t LOG_INTERVAL_MS = 1000;

} // namespace Config

static uint8_t s88Data[Config::S88_BYTE_COUNT] = {};
static uint32_t lastLogMs = 0;

static inline void waitControlPulse() {
    delayMicroseconds(Config::S88_CONTROL_PULSE_US);
}

static inline void setClock(bool high) {
    digitalWrite(Config::S88_CLOCK_PIN, high ? HIGH : LOW);
    delayMicroseconds(Config::S88_HALF_CLOCK_US);
}

static inline bool readDataPin() {
    return digitalRead(Config::S88_DATA_PIN) == HIGH;
}

static void storeBit(uint16_t bitIndex, bool value) {
    if (!value) {
        return;
    }

    const uint8_t byteIndex = bitIndex / 8;
    const uint8_t bitInByte = bitIndex % 8;
    s88Data[byteIndex] |= static_cast<uint8_t>(1U << bitInByte);
}

static void clearData() {
    for (uint8_t i = 0; i < Config::S88_BYTE_COUNT; ++i) {
        s88Data[i] = 0;
    }
}

static void readS88() {
    clearData();

    // Bus idle state.
    digitalWrite(Config::S88_CLOCK_PIN, LOW);
    digitalWrite(Config::S88_LOAD_PIN, LOW);
    digitalWrite(Config::S88_RESET_PIN, LOW);
    waitControlPulse();

    // 1) LOAD / PS goes HIGH.
    // 2) First clock pulse transfers the current parallel input states
    //    into the S88 shift register.
    digitalWrite(Config::S88_LOAD_PIN, HIGH);
    waitControlPulse();

    setClock(true);
    setClock(false);

    // The first feedback bit is available after the first clock pulse.
    storeBit(0, readDataPin());

    // Reset the input latches so they can capture new events while the
    // already loaded snapshot is shifted out.
    digitalWrite(Config::S88_RESET_PIN, HIGH);
    waitControlPulse();
    digitalWrite(Config::S88_RESET_PIN, LOW);
    waitControlPulse();

    // End LOAD phase and shift out the remaining bits.
    digitalWrite(Config::S88_LOAD_PIN, LOW);
    waitControlPulse();

    for (uint16_t bitIndex = 1; bitIndex < Config::S88_INPUT_COUNT; ++bitIndex) {
        setClock(true);
        setClock(false);
        storeBit(bitIndex, readDataPin());
    }

    // Return to a defined idle state.
    digitalWrite(Config::S88_CLOCK_PIN, LOW);
    digitalWrite(Config::S88_LOAD_PIN, LOW);
    digitalWrite(Config::S88_RESET_PIN, LOW);
}

static void printByteBinary(uint8_t value) {
    Serial.print('_');
    for (int8_t bit = 7; bit >= 0; --bit) {
        Serial.print((value & (1U << bit)) ? '1' : '0');
    }
}

static void printS88Data() {
    Serial.print(F("S88:"));

    for (uint8_t i = 0; i < Config::S88_BYTE_COUNT; ++i) {
        Serial.print(' ');
        printByteBinary(s88Data[i]);
    }

    Serial.println();
}

void setup() {
    pinMode(Config::S88_CLOCK_PIN, OUTPUT);
    pinMode(Config::S88_LOAD_PIN, OUTPUT);
    pinMode(Config::S88_RESET_PIN, OUTPUT);
    pinMode(Config::S88_DATA_PIN, INPUT); // No internal pull-up on S88 DATA.

    digitalWrite(Config::S88_CLOCK_PIN, LOW);
    digitalWrite(Config::S88_LOAD_PIN, LOW);
    digitalWrite(Config::S88_RESET_PIN, LOW);

    Serial.begin(Config::SERIAL_BAUD);
    delay(500);

    Serial.println();
    Serial.println(F("DCCExpress S88Adapter v0.1.0"));
    Serial.println(F("Mode: S88 -> Serial test"));
    Serial.print(F("S88 modules: "));
    Serial.print(Config::S88_MODULE_COUNT);
    Serial.print(F("  inputs: "));
    Serial.println(Config::S88_INPUT_COUNT);
    Serial.println(F("Pins: CLOCK=D2 LOAD=D3 RESET=D4 DATA=D5"));
    Serial.println(F("Serial format example: S88: _00000001 _00000000"));
    Serial.println();
}

void loop() {
    const uint32_t now = millis();

    if (now - lastLogMs >= Config::LOG_INTERVAL_MS) {
        lastLogMs = now;
        readS88();
        printS88Data();
    }
}

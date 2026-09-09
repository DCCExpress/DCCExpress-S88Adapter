#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// DCCExpress S88Adapter configuration
// -----------------------------------------------------------------------------

// Arduino Uno I2C slave address.
//
// 0x30 is the default address expected by DCCExpressHub.
// Valid normal 7-bit I2C device addresses are 0x08..0x77.
#ifndef S88_I2C_ADDRESS
#define S88_I2C_ADDRESS 0x30
#endif

// Number of traditional 16-input S88 modules in the chain.
#ifndef S88_MODULE_COUNT
#define S88_MODULE_COUNT 1
#endif

// S88 clock:
// 20 us HIGH + 20 us LOW = 25 kHz.
#ifndef S88_HALF_CLOCK_US
#define S88_HALF_CLOCK_US 20
#endif

// Keep LOAD / RESET pulses conservative.
#ifndef S88_CONTROL_PULSE_US
#define S88_CONTROL_PULSE_US 50
#endif

// Refresh the S88 snapshot frequently so the I2C master receives current data.
#ifndef S88_READ_INTERVAL_MS
#define S88_READ_INTERVAL_MS 20UL
#endif

// Human-readable serial debug output interval.
#ifndef S88_LOG_INTERVAL_MS
#define S88_LOG_INTERVAL_MS 1000UL
#endif

static_assert(
    S88_I2C_ADDRESS >= 0x08 &&
    S88_I2C_ADDRESS <= 0x77,
    "S88_I2C_ADDRESS must be a normal 7-bit I2C address (0x08..0x77)");

static_assert(
    S88_MODULE_COUNT >= 1,
    "S88_MODULE_COUNT must be at least 1");

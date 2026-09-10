#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// DCCExpress S88Adapter defaults
// -----------------------------------------------------------------------------

#ifndef S88_I2C_ADDRESS
#define S88_I2C_ADDRESS 0x30
#endif

// Used until DCCExpressHub sends the runtime group count.
#ifndef S88_DEFAULT_GROUP_COUNT
#define S88_DEFAULT_GROUP_COUNT 1
#endif

// AVR Wire TX buffer is 32 bytes.
// One S88 group = 16 sensors = 2 bytes.
#ifndef S88_MAX_GROUP_COUNT
#define S88_MAX_GROUP_COUNT 16
#endif

#ifndef S88_HALF_CLOCK_US
#define S88_HALF_CLOCK_US 20
#endif

#ifndef S88_CONTROL_PULSE_US
#define S88_CONTROL_PULSE_US 50
#endif

#ifndef S88_READ_INTERVAL_MS
#define S88_READ_INTERVAL_MS 20UL
#endif

#ifndef S88_LOG_INTERVAL_MS
#define S88_LOG_INTERVAL_MS 1000UL
#endif

static_assert(
    S88_I2C_ADDRESS >= 0x08 &&
    S88_I2C_ADDRESS <= 0x77,
    "S88_I2C_ADDRESS must be a normal 7-bit I2C address");

static_assert(
    S88_DEFAULT_GROUP_COUNT >= 1 &&
    S88_DEFAULT_GROUP_COUNT <= S88_MAX_GROUP_COUNT,
    "Invalid S88 default group count");

static_assert(
    S88_MAX_GROUP_COUNT <= 16,
    "16 S88 groups = 32 bytes, the AVR Wire buffer limit");

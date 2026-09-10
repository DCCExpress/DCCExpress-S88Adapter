#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// DCCExpress S88Adapter defaults
//
// The adapter owns its hardware configuration. I2C address and S88 byte length
// are configured from the USB serial console and persisted to EEPROM.
// The Hub only reads adapter INFO and snapshots; it never changes these values.
// -----------------------------------------------------------------------------

#ifndef S88_ADAPTER_VERSION
#define S88_ADAPTER_VERSION "0.5.0"
#endif

#ifndef S88_ADAPTER_VERSION_MAJOR
#define S88_ADAPTER_VERSION_MAJOR 0
#endif

#ifndef S88_ADAPTER_VERSION_MINOR
#define S88_ADAPTER_VERSION_MINOR 5
#endif

#ifndef S88_ADAPTER_VERSION_PATCH
#define S88_ADAPTER_VERSION_PATCH 0
#endif

#ifndef S88_I2C_ADDRESS
#define S88_I2C_ADDRESS 0x30
#endif

// Default 2 bytes = 16 S88 inputs.
#ifndef S88_DEFAULT_GROUP_COUNT
#define S88_DEFAULT_GROUP_COUNT 2
#endif

// AVR Wire TX buffer is 32 bytes:
// 32 bytes * 8 sensors = 256 sensors.
#ifndef S88_MAX_GROUP_COUNT
#define S88_MAX_GROUP_COUNT 32
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
    "Invalid S88 default byte count");

static_assert(
    S88_MAX_GROUP_COUNT <= 32,
    "AVR Wire can return at most 32 S88 bytes");

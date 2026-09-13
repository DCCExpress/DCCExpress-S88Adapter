# DCCExpress-S88Adapter

Arduino-based **S88 / s88-N feedback adapter** for the [DCCExpressHub](https://github.com/DCCExpress/DCCExpressHub) ecosystem.

The adapter reads S88 occupancy feedback, exposes the current feedback snapshot to the DCCExpressHub over I2C, and provides a USB serial console for configuration and diagnostics.

The current firmware is intended to be a complete, usable adapter implementation for Arduino Uno-class ATmega328P boards.

## Current firmware

Current firmware version:

```text
v0.5.0
```

Main features:

- S88 / s88-N feedback input
- up to **32 S88 bytes / 256 feedback inputs**
- configurable active S88 byte count
- configurable I2C slave address
- configuration stored in Arduino EEPROM
- USB serial configuration console
- periodic S88 diagnostic logging
- read-only I2C protocol for DCCExpressHub
- adapter INFO packet with firmware/protocol/capability information
- direct S88 snapshot reads over I2C
- tested with a **YaMoRC YD6016ES-CS**

## Architecture

The adapter owns its own hardware configuration.

```text
S88 / s88-N feedback modules
          |
          v
+--------------------------+
| Arduino Uno              |
| DCCExpress S88Adapter    |
|                          |
| S88 master               |
| USB serial configuration |
| EEPROM configuration     |
| I2C slave                |
+--------------------------+
          |
          | I2C
          v
+--------------------------+
| DCCExpressHub / ESP32    |
+--------------------------+
```

The DCCExpressHub can request adapter information and read the current S88 snapshot. The Hub **does not change adapter configuration**.

I2C address and S88 byte length are configured locally through the Arduino USB serial console.

## Supported hardware

Recommended / tested controller:

- Arduino Uno
- compatible ATmega328P Uno boards

The firmware currently assumes the standard Uno pin assignment shown below.

## Arduino pin assignment

| Function | Arduino Uno |
|---|---:|
| S88 CLOCK | D2 |
| S88 PS / LOAD | D3 |
| S88 RESET | D4 |
| S88 DATA | D5 |
| I2C SDA | A4 |
| I2C SCL | A5 |
| GND | GND |

On many Uno-compatible boards the silkscreen only shows `2`, `3`, `4`, `5`. These correspond to `D2`, `D3`, `D4`, `D5`.

## s88-N RJ45 pinout

s88-N uses an 8P8C / RJ45 connector and normal Ethernet-style twisted-pair cable.

| RJ45 pin | s88-N signal | Arduino Uno |
|---:|---|---|
| 1 | +5 V / +12 V bus supply | 5 V only when suitable for the module |
| 2 | DATA | D5 |
| 3 | GND | GND |
| 4 | CLOCK | D2 |
| 5 | GND | GND |
| 6 | PS / LOAD | D3 |
| 7 | RESET | D4 |
| 8 | RAILDATA | not used |

### Important: bus voltage

The general s88-N specification allows different bus supply voltages depending on the connected hardware.

**Do not assume every s88-N module accepts 12 V.**

For the tested YaMoRC YD6016ES-CS:

> **Use maximum 5 V on the s88 bus. Do not feed 12 V into the module.**

Disconnect power before changing wiring.

## RJ45 cable requirements

Use a **full 8-conductor straight-through Ethernet patch cable**, preferably CAT5, CAT5e or CAT6.

A cable tester should show:

```text
1 -> 1
2 -> 2
3 -> 3
4 -> 4
5 -> 5
6 -> 6
7 -> 7
8 -> 8
```

Do not use 4-conductor Ethernet cables wired only for 10/100 Mbit Ethernet. Such a cable cannot carry all required s88-N signals.

Typical symptoms include:

- feedback module powers up but no feedback data appears,
- S88 snapshot remains all zero,
- CLOCK or RESET never reaches the detector.

When debugging S88 hardware, verify the cable before changing firmware.

## RJ45 wire colors

The **pin number matters; wire color alone is not enough** because Ethernet cables may use either T568A or T568B.

With the gold contacts facing you and the locking clip on the opposite/down side:

```text
1  2  3  4  5  6  7  8
```

Typical T568B:

| Pin | Color |
|---:|---|
| 1 | white/orange |
| 2 | orange |
| 3 | white/green |
| 4 | blue |
| 5 | white/blue |
| 6 | green |
| 7 | white/brown |
| 8 | brown |

Typical T568A:

| Pin | Color |
|---:|---|
| 1 | white/green |
| 2 | green |
| 3 | white/orange |
| 4 | blue |
| 5 | white/blue |
| 6 | orange |
| 7 | white/brown |
| 8 | brown |

Use a continuity meter or cable tester when connecting a screw-terminal RJ45 adapter.

## Tested feedback module

The current implementation has been tested with the **YaMoRC YD6016ES-CS**.

For this module:

- connect the Arduino / S88 master side to **s88N OUT**,
- additional downstream S88 modules connect through **s88N IN**,
- use maximum **5 V** on the s88-N bus,
- the green status LED may light or blink while S88 communication is active,
- the red occupancy indicator may light when detector inputs sense current.

Practical wiring:

```text
RJ45 pin 1 -> Uno 5V
RJ45 pin 2 -> Uno D5   DATA
RJ45 pin 3 -> Uno GND
RJ45 pin 4 -> Uno D2   CLOCK
RJ45 pin 5 -> Uno GND
RJ45 pin 6 -> Uno D3   PS / LOAD
RJ45 pin 7 -> Uno D4   RESET
RJ45 pin 8 -> not connected
```

Refer to the YaMoRC manual before combining s88-N and ES-Link connections.

## Default configuration

Firmware defaults:

```text
I2C address:       0x30
S88 bytes:         2
S88 inputs:        16
Maximum bytes:     32
Maximum inputs:    256
Serial baud rate:  115200
```

One S88 byte represents eight feedback inputs.

```text
2 bytes  = 16 inputs
4 bytes  = 32 inputs
8 bytes  = 64 inputs
16 bytes = 128 inputs
32 bytes = 256 inputs
```

The maximum of 32 bytes is based on the AVR Wire transmit buffer size.

## S88 timing

Current default timing:

```text
CLOCK HIGH: 20 us
CLOCK LOW:  20 us
```

This gives an S88 clock of approximately:

```text
25 kHz
```

Control pulse timing:

```text
50 us
```

The adapter refreshes the S88 snapshot approximately every `20 ms`.

Periodic serial logging defaults to once per second.

# Serial configuration console

The USB serial console is used to configure and diagnose the adapter.

## IMPORTANT serial monitor settings

Use:

```text
Baud rate:   115200
Line ending: Newline (LF)
```

`Both NL & CR` also works.

### Do not use

```text
No line ending
Carriage return only
```

The command parser executes a command only when it receives a newline (`LF`, `\n`).

So if you type:

```text
HELP
```

and nothing happens, check the serial monitor line-ending setting first.

PlatformIO serial monitor:

```bash
pio device monitor
```

## Available serial commands

Type `HELP` or `?` to display the command list.

```text
HELP
STATUS
INFO
READ
SET ADDRESS <address>
SET I2C <address>
SET BYTES <count>
SET GROUPS <count>
SAVE
DEFAULTS
LOG ON
LOG OFF
REBOOT
RESET
```

Commands are case-insensitive, so `HELP`, `help`, `Help` and `?` are all valid.

### HELP

Display available commands:

```text
HELP
```

### STATUS

Show active, staged and saved configuration:

```text
STATUS
```

Alias:

```text
SHOW
```

The output includes firmware version, protocol version, uptime, active/staged/saved I2C address, active/staged/saved S88 length, EEPROM status, unsaved changes and periodic logging state.

### INFO

Show firmware, protocol and hardware information:

```text
INFO
```

### READ

Print the current S88 snapshot immediately:

```text
READ
```

Example:

```text
S88: _00000001 _00000000
```

The rightmost bit of the first byte represents feedback input 1.

All inputs inactive:

```text
S88: _00000000 _00000000
```

Input 1 active:

```text
S88: _00000001 _00000000
```

### SET ADDRESS

Stage a new I2C slave address:

```text
SET ADDRESS 0x30
```

Valid normal 7-bit I2C address range:

```text
0x08 .. 0x77
```

Alias:

```text
SET I2C 0x30
```

Changing the I2C address requires:

```text
SET ADDRESS 0x31
SAVE
REBOOT
```

The currently active I2C address remains in use until reboot.

### SET BYTES

Set the active S88 snapshot length:

```text
SET BYTES 4
```

This means:

```text
4 bytes = 32 S88 inputs
```

Valid range:

```text
1 .. 32 bytes
```

Alias:

```text
SET GROUPS 4
```

Byte-count changes are applied immediately to the running adapter. Use `SAVE` to keep the new byte count after reboot.

### SAVE

Store the staged I2C address and S88 byte count in EEPROM:

```text
SAVE
```

If the I2C address changed, reboot after saving.

### DEFAULTS

Stage the firmware defaults:

```text
DEFAULTS
```

Current defaults:

```text
I2C address: 0x30
S88 bytes:   2
```

`DEFAULTS` does **not** save automatically.

To make the defaults persistent:

```text
DEFAULTS
SAVE
REBOOT
```

### LOG ON / LOG OFF

Enable periodic S88 snapshot output:

```text
LOG ON
```

Disable it:

```text
LOG OFF
```

Periodic logging is enabled by default after boot.

### REBOOT / RESET

Reboot the Arduino:

```text
REBOOT
```

Alias:

```text
RESET
```

This is required after changing and saving the I2C address.

# Configuration model

The adapter maintains three related configuration states.

## Active configuration

Values currently used by the running firmware.

## Staged configuration

Values edited through the serial console.

Example:

```text
SET ADDRESS 0x31
SET BYTES 4
```

## Saved configuration

Values stored in EEPROM using:

```text
SAVE
```

On boot, valid EEPROM configuration overrides firmware defaults. If EEPROM configuration is invalid or absent, firmware defaults are used.

# I2C interface

The Arduino acts as an I2C slave.

Default address:

```text
0x30
```

Arduino Uno pins:

```text
SDA = A4
SCL = A5
```

## Ownership model

The DCCExpressHub has read-only access.

The Hub can:

- request adapter INFO,
- read the current S88 snapshot.

The Hub cannot:

- change I2C address,
- change S88 byte count,
- write adapter configuration.

Configuration is intentionally controlled only through USB serial.

## Normal snapshot read

A normal I2C read returns the current raw S88 snapshot. The Hub requests exactly the number of bytes advertised by the adapter.

Each byte contains eight S88 feedback states.

## INFO request

The Hub can request a fixed adapter information packet.

I2C write selector:

```text
byte 0 = 0xA5
byte 1 = 0x02
byte 2 = XOR checksum of byte 0 and byte 1
```

Checksum:

```text
0xA5 XOR 0x02 = 0xA7
```

So the request packet is:

```text
A5 02 A7
```

The next I2C read returns the 10-byte INFO packet.

## INFO response

```text
[0]  0xA5  protocol magic
[1]  0x82  INFO response
[2]  protocol version
[3]  firmware major
[4]  firmware minor
[5]  firmware patch
[6]  active S88 byte count
[7]  maximum supported byte count
[8]  capability flags
[9]  XOR checksum of bytes 0..8
```

Current protocol version:

```text
1
```

Current capability flags indicate serial configuration and EEPROM configuration support.

The INFO selection applies only to the next I2C read. After that, reads automatically return to normal S88 snapshot mode.

# Build with PlatformIO

Environment:

```ini
[env:uno]
platform = atmelavr
board = uno
framework = arduino
monitor_speed = 115200
```

Build:

```bash
pio run
```

Upload:

```bash
pio run -t upload
```

Serial monitor:

```bash
pio device monitor
```

Remember: `115200 baud` and `Newline / LF` line ending for commands.

# Typical first setup

1. Flash the adapter.
2. Connect the Arduino through USB.
3. Open the serial monitor at `115200`.
4. Configure line ending as `Newline (LF)` or `Both NL & CR`.
5. Type:

```text
HELP
```

6. Check current configuration:

```text
STATUS
```

7. For the default 16-input setup, no configuration change is required.
8. For 32 feedback inputs:

```text
SET BYTES 4
SAVE
```

9. To change the I2C address:

```text
SET ADDRESS 0x31
SAVE
REBOOT
```

10. Verify with `STATUS` and test feedback with `READ`.

# Troubleshooting

## HELP does nothing

Check:

```text
Baud:        115200
Line ending: Newline / LF
```

`No line ending` will not execute commands.

`Carriage return only` will not execute commands.

## Serial output is unreadable

Check that the serial monitor is set to `115200 baud`.

## S88 always reads zero

Check:

- detector power,
- GND connection,
- RJ45 pin mapping,
- full 8-conductor Ethernet cable,
- CLOCK on D2,
- LOAD on D3,
- RESET on D4,
- DATA on D5.

If using a YaMoRC YD6016ES-CS, check that the green LED indicates module power/activity.

## S88 module is powered but no data appears

A common cause is a 4-conductor Ethernet cable. Use a cable tester and verify all eight pins are connected straight through.

## New I2C address does not respond

Changing the address requires:

```text
SET ADDRESS <new address>
SAVE
REBOOT
```

Check the result with `STATUS`.

## Byte count changes after reboot

Use `SAVE` after `SET BYTES ...`.

# Safety notes

- Disconnect power before rewiring the S88 bus.
- Verify the required S88 bus voltage for the connected feedback module.
- Do not feed 12 V into a YaMoRC YD6016ES-CS s88-N connection.
- Verify RJ45 pin numbering instead of trusting wire colors.
- Use a full 8-conductor straight-through cable.
- When connecting an ESP32-based Hub to a 5 V Arduino Uno over I2C, use an appropriate bidirectional level shifter / voltage-level interface.

# Project status

The S88 adapter is currently considered feature-complete for the intended DCCExpressHub integration:

- S88 acquisition: implemented
- configurable byte/input count: implemented
- I2C slave snapshot interface: implemented
- INFO protocol: implemented
- USB serial configuration: implemented
- EEPROM persistence: implemented
- status / diagnostic console: implemented
- YaMoRC YD6016ES-CS compatibility: tested

Future changes can focus on additional hardware support, diagnostics or protocol extensions rather than the basic adapter functionality.

# References

- s88-N specification: https://s88-n.eu/en/
- s88-N timing: https://s88-n.eu/en/s88-timing.html
- YaMoRC YD6016ES-CS manual: https://www.yamorc.de/downloads/YD6016ES-CS.de.pdf
- YaMoRC YD6016ES-CS product information: https://yamorc.de/upcp_product/yd6016es-cs/

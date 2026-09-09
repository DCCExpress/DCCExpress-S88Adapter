# DCCExpress-S88Adapter

Arduino-based S88 feedback adapter for the DCCExpress ecosystem.

## v0.1.0 - S88 to Serial test

The first version intentionally does only one thing:

- reads one traditional S88 module (16 inputs),
- stores the state as two bytes,
- prints the bytes to the Arduino serial port once per second.

There is no I2C and no DCC-EX integration yet. The goal of this version is to verify the S88 electrical connection, timing and bit order first.

## Target hardware

- Arduino Uno
- one S88 / s88-N feedback module

## Arduino pin assignment

| S88 signal | Arduino Uno |
|---|---:|
| CLOCK | D2 |
| PS / LOAD | D3 |
| RESET | D4 |
| DATA | D5 |
| GND | GND |

For s88-N (RJ45), the standardized signal pins are:

| RJ45 pin | Signal |
|---:|---|
| 1 | +5 V / +12 V |
| 2 | DATA |
| 3 | GND |
| 4 | CLOCK |
| 5 | GND |
| 6 | PS / LOAD |
| 7 | RESET |
| 8 | RAILDATA |

**Important:** this first firmware test defines the signal wiring only. Do not blindly connect the RJ45 power pin to the Arduino 5 V output. Power the feedback module according to its own manual until the exact detector hardware and required bus supply are confirmed.

## Serial monitor

Baud rate:

```text
115200
```

Example with all inputs inactive:

```text
S88: _00000000 _00000000
```

If feedback input 1 is active:

```text
S88: _00000001 _00000000
```

The rightmost bit of the first byte is feedback input 1.

## Build with PlatformIO

```bash
pio run
```

Upload to an Arduino Uno:

```bash
pio run -t upload
```

Open the serial monitor:

```bash
pio device monitor
```

## Current timing

The test firmware uses a conservative 10 kHz S88 clock:

- CLOCK HIGH: 50 us
- CLOCK LOW: 50 us

This is intentionally slow for the first hardware test.

## Roadmap

- v0.1: S88 -> Serial
- v0.2: configurable S88 module count
- v0.3: Arduino I2C slave exposing S88 bytes
- later: DCC-EX / EX-CSB1 integration

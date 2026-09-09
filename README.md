# DCCExpress-S88Adapter

Arduino-based S88 / s88-N feedback adapter for the DCCExpress ecosystem.

The first development step is intentionally simple: read one 16-input S88 feedback module with an Arduino Uno and print the detected states to the serial monitor. I2C and DCC-EX integration will be added after the electrical interface, timing and bit order are verified.

## Current version: S88 -> Serial test

Current functionality:

- reads one S88 / s88-N module with 16 feedback inputs,
- stores the state as two bytes,
- prints the bytes once per second over USB serial,
- uses a conservative 10 kHz S88 clock.

Example:

```text
S88: _00000001 _00000000
```

The rightmost bit of the first byte represents feedback input 1.

## Hardware

Current test setup:

- Arduino Uno or compatible ATmega328P Uno board,
- one S88 / s88-N feedback module,
- for s88-N: RJ45 breakout / screw-terminal adapter,
- full 8-conductor Ethernet patch cable,
- optional cable tester, strongly recommended.

The current test setup has been verified with a **YaMoRC YD6016ES-CS**, which is a standard s88-N 16-input current-sensing feedback module.

## Arduino wiring

The firmware currently uses:

| S88 signal | Arduino Uno |
|---|---:|
| CLOCK | D2 / pin 2 |
| PS / LOAD | D3 / pin 3 |
| RESET | D4 / pin 4 |
| DATA | D5 / pin 5 |
| GND | GND |

On many Uno-compatible boards the silkscreen only shows `2`, `3`, `4`, `5`. These are the same pins commonly referred to as `D2`, `D3`, `D4`, `D5`.

## s88-N RJ45 pinout

s88-N standardizes the S88 bus on an 8P8C/RJ45 connector and twisted-pair network cable.

| RJ45 pin | s88-N signal | Arduino Uno in this project |
|---:|---|---|
| 1 | +5 V / +12 V bus supply | 5 V **only when appropriate for the connected module** |
| 2 | DATA | D5 |
| 3 | GND | GND |
| 4 | CLOCK | D2 |
| 5 | GND | GND |
| 6 | PS / LOAD | D3 |
| 7 | RESET | D4 |
| 8 | RAILDATA | not used by the current firmware |

The s88-N specification allows the bus supply on pin 1 to be either 5 V or 12 V, depending on the equipment. **Never assume that every s88-N module is 12 V tolerant.**

### RJ45 pin 1 and wire colors

The **pin number is standardized; the wire color is not sufficient to identify it**, because Ethernet cables may use either T568A or T568B termination.

When looking at an RJ45 plug with the gold contacts facing you and the locking clip on the opposite/down side, the pins are numbered from left to right:

```text
1  2  3  4  5  6  7  8
```

For a T568B cable:

| Pin | Typical color |
|---:|---|
| 1 | white/orange |
| 2 | orange |
| 3 | white/green |
| 4 | blue |
| 5 | white/blue |
| 6 | green |
| 7 | white/brown |
| 8 | brown |

For a T568A cable:

| Pin | Typical color |
|---:|---|
| 1 | white/green |
| 2 | green |
| 3 | white/orange |
| 4 | blue |
| 5 | white/blue |
| 6 | orange |
| 7 | white/brown |
| 8 | brown |

So **pin 1 is white/orange only on a T568B cable**. On T568A it is white/green.

Do not rely on color when connecting a screw-terminal RJ45 adapter. Verify the adapter and cable with a continuity meter or cable tester.

## Ethernet cable requirements

This is important.

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

### Cables that are NOT suitable

Some inexpensive or older Ethernet cables contain only four conductors and are wired only for 10/100 Mbit Ethernet:

```text
1
2
3
6
```

Such a cable may work perfectly for 100BASE-TX Ethernet but **will not work correctly for s88-N**.

With s88-N it would lose, among others:

```text
pin 4 = CLOCK
pin 5 = GND
pin 7 = RESET
pin 8 = RAILDATA
```

A typical symptom is that the feedback module is powered or partially alive, but the Arduino reads only zeros because CLOCK and/or RESET never reaches the module.

**Always test an unknown cable before debugging the firmware.**

A normal 5 m full 8-conductor patch cable is perfectly reasonable for this test setup.

## YaMoRC YD6016ES-CS

The YaMoRC YD6016ES-CS is explicitly designed as a standard **s88-N** feedback module and uses normal RJ45 connectors and standard Ethernet patch cables.

For this module:

- connect the Arduino / s88 master side to the YD6016ES-CS **s88N OUT** connector,
- additional downstream s88-N modules are connected through **s88N IN**,
- the module can receive power from ES-Link or from the s88 bus,
- when powered from the s88 bus, the YD6016ES-CS requires **5 V maximum**.

### YaMoRC status LED during testing

With the YD6016ES-CS correctly powered from the s88-N bus and connected to the Arduino master, the **green LED on the module lights and may blink during bus activity**.

This is a useful practical check during bring-up:

```text
green LED off
    -> check s88-N power, cable and wiring first

green LED on / blinking
    -> the module is powered and s88-N activity is present
```

During our test, the green LED became active immediately after replacing a partially wired 4-conductor Ethernet cable with a proper 8-conductor patch cable and correcting the RJ45 pin mapping.

The red occupancy indicator may also light when one or more detector inputs sense current.

### Important YaMoRC power warning

**Do not feed 12 V into the s88 bus when a YD6016ES-CS is connected.**

Although the general s88-N specification permits either 5 V or 12 V bus supply depending on the equipment, YaMoRC specifies the YD6016ES-CS for **maximum 5 V on s88** and warns that 12 V can destroy the module.

For the current Arduino Uno test:

```text
RJ45 pin 1  -> Uno 5V
RJ45 pin 2  -> Uno D5   DATA
RJ45 pin 3  -> Uno GND
RJ45 pin 4  -> Uno D2   CLOCK
RJ45 pin 5  -> Uno GND
RJ45 pin 6  -> Uno D3   PS / LOAD
RJ45 pin 7  -> Uno D4   RESET
RJ45 pin 8  -> not connected
```

Disconnect power before changing wiring.

YaMoRC also warns against simultaneously using the corresponding ES-Link and s88-N connections on the same side of the module. Refer to the YD6016ES-CS manual when changing between ES-Link and s88-N operation.

## Serial monitor

Baud rate:

```text
115200
```

All inputs inactive:

```text
S88: _00000000 _00000000
```

Feedback input 1 active:

```text
S88: _00000001 _00000000
```

## Current timing

The test firmware uses a conservative 10 kHz S88 clock:

```text
CLOCK HIGH: 50 us
CLOCK LOW:  50 us
```

This is intentionally slow and is suitable for initial compatibility testing.

## Build with PlatformIO

Build:

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

## Roadmap

- v0.1: S88 -> Serial
- v0.2: configurable S88 module count
- v0.3: Arduino I2C slave exposing S88 bytes
- later: DCC-EX / EX-CSB1 integration

## References

- s88-N specification: https://s88-n.eu/en/
- s88-N timing: https://s88-n.eu/en/s88-timing.html
- YaMoRC YD6016ES-CS manual: https://www.yamorc.de/downloads/YD6016ES-CS.de.pdf
- YaMoRC YD6016ES-CS product information: https://yamorc.de/upcp_product/yd6016es-cs/

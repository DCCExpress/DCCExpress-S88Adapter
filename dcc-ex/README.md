# DCC-EX integration

This directory contains the **CommandStation-EX HAL integration** for
DCCExpress-S88Adapter.

The intended data path is:

```text
S88 / s88-N modules
        |
        v
DCCExpress-S88Adapter
    Arduino Uno
        |
       I2C
        |
        v
DCC-EX CommandStation-EX
        |
   <Q>/<q> sensor events
        |
        +----> DCCExpressHub
        +----> JMRI
        +----> other DCC-EX clients
```

The adapter does not send DCCExpressHub-specific packets over the command-station
connection. DCC-EX reads the adapter over I2C, exposes the S88 inputs as VPINs,
and normal DCC-EX Sensor objects generate the standard `<Q ID>` /
`<q ID>` events.

## Files

- `IO_DCCExpressS88.h` — DCC-EX IODevice/HAL driver.
- `myHal.example.cpp` — example DCC-EX `myHal.cpp` integration.
- `sensors-1001-1032.txt` — equivalent 32-sensor DCC-EX command definitions.

## 1. Configure the adapter first

The adapter defaults to:

```text
I2C address: 0x30
S88 bytes:   2
S88 inputs:  16
```

For 32 feedback inputs, use the adapter USB serial console at 115200 baud:

```text
SET BYTES 4
SAVE
```

If you change the I2C address:

```text
SET ADDRESS 0x31
SAVE
REBOOT
```

The adapter must expose at least as many inputs as the DCC-EX HAL instance
requests.

## 2. Wire I2C to the DCC-EX controller

Arduino Uno adapter pins:

```text
SDA = A4
SCL = A5
GND = GND
```

Connect SDA, SCL and GND to the DCC-EX controller's I2C bus.

Check the controller's logic voltage. The adapter is an Uno-class 5 V device;
use appropriate I2C level shifting when the DCC-EX controller uses 3.3 V logic
and is not 5 V tolerant.

## 3. Add the HAL driver to CommandStation-EX

Copy:

```text
IO_DCCExpressS88.h
```

into the CommandStation-EX configuration/source location where your
`myHal.cpp` can include it.

Then merge the relevant lines from `myHal.example.cpp` into your existing
DCC-EX `myHal.cpp`.

Do not blindly replace an existing `myHal.cpp`.

Example:

```cpp
#include "IO_DCCExpressS88.h"
#include "Sensors.h"

void halSetup() {
    constexpr int FIRST_VPIN = 1001;
    constexpr int SENSOR_COUNT = 32;

    DCCExpressS88::create(FIRST_VPIN, SENSOR_COUNT, 0x30);

    for (int i = 0; i < SENSOR_COUNT; i++) {
        const int id = FIRST_VPIN + i;
        Sensor::create(id, id, 0);
    }
}
```

This maps:

```text
S88 input 1  -> VPIN/sensor 1001
S88 input 2  -> VPIN/sensor 1002
...
S88 input 32 -> VPIN/sensor 1032
```

The I2C address in `DCCExpressS88::create(...)` must match the adapter's
configured I2C address.

## 4. Sensor definitions

The example above creates the DCC-EX Sensor objects in `myHal.cpp`.

Equivalent runtime sensor definitions are:

```text
<S 1001 1001 0>
<S 1002 1002 0>
...
<S 1032 1032 0>
<E>
```

The full example is in `sensors-1001-1032.txt`.

Once the Sensor objects exist, DCC-EX broadcasts ordinary sensor changes:

```text
<Q 1001>   sensor active / occupied
<q 1001>   sensor inactive / free
```

DCCExpressHub can then consume those normal DCC-EX sensor events without any
special S88 transport on the Hub side.

## 5. Verify the complete chain

Recommended test order:

```text
1. Adapter serial console -> READ
2. Toggle one physical S88 input
3. Verify the adapter S88 snapshot changes
4. Verify DCC-EX emits <Q ID> / <q ID>
5. Verify the same sensor changes in DCCExpressHub/JMRI
```

If DCC-EX reports that the requested input count is larger than the adapter
provides, increase the adapter byte count with `SET BYTES ...`, then `SAVE`.

For S88 wiring, adapter firmware configuration, timing and troubleshooting,
see the repository root README.

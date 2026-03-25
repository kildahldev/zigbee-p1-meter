# Zigbee P1 Meter

Custom firmware for the **Homey Energy Dongle** (ESP32-C6) that reads DSMR 5.0 telegrams from a smart meter P1 port and exposes 3-phase electrical data over Zigbee.

Probably also works for other similar hardware.

Should work with any DSMR 5.0 compliant meter (Landis+Gyr, Kaifa, Kamstrup, Iskra, Sagemcom, etc).


Was written for my use case where I had a Homey Energy Dongle but wanted to utilize the Zigbee capability of the C6 instead of wifi. Can probably be improved a bunch and/or support more devices, feel free to write a PR.

Here is an example of how you can use the data in a dashboard
<img width="2864" height="1534" alt="image" src="https://github.com/user-attachments/assets/55518db7-f02f-4c25-b29f-738807bcd441" />


## Build & Flash

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
```

Flash requires manual boot mode: **hold pinhole reset while plugging USB-C**, then:

```bash
idf.py flash
```

## Exposed Data

| Attribute | Unit | Description |
|-----------|------|-------------|
| `power` | W | Total active power (import - export) |
| `power_phase_a` | W | Active power phase A |
| `power_phase_b` | W | Active power phase B |
| `power_phase_c` | W | Active power phase C |
| `voltage_phase_a` | V | RMS voltage phase A |
| `voltage_phase_b` | V | RMS voltage phase B |
| `voltage_phase_c` | V | RMS voltage phase C |
| `current_phase_a` | A | RMS current phase A |
| `current_phase_b` | A | RMS current phase B |
| `current_phase_c` | A | RMS current phase C |
| `energy` | kWh | Total energy consumed (cumulative) |
| `produced_energy` | kWh | Total energy returned to grid (cumulative) |

Values update on every DSMR telegram (typically every 10 seconds).

## Z2M Setup
Add a new external converter to Z2M, either through the UI or in the file system like described below
1. Copy `z2m/homey_p1_meter.mjs` to your Z2M data/external_converters directory
2. Add to `configuration.yaml`:
   ```yaml
   external_converters:
     - homey_p1_meter.mjs
   ```
3. Restart Z2M and enable pairing

## LED

Blue breathing = pairing, green = connected, green blink = telegram rx, purple pulse = OTA, red = no P1 data

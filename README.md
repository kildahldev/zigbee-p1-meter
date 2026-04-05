# Zigbee P1 Meter

Custom firmware for the **Homey Energy Dongle** (ESP32-C6) that reads DSMR 5.0 telegrams from a smart meter P1 port and exposes 3-phase electrical data over Zigbee.

Probably also works for other similar hardware.

Should work with any DSMR 5.0 compliant meter (Landis+Gyr, Kaifa, Kamstrup, Iskra, Sagemcom, etc).


Was written for my use case where I had a Homey Energy Dongle but wanted to utilize the Zigbee capability of the C6 instead of wifi. Can probably be improved a bunch and/or support more devices, feel free to write a PR

## Flashing the Firmware

Pre-built firmware is available on the [Releases](../../releases) page

### Entering Flash Mode

The dongle has no dedicated boot button. To enter flash mode:

1. **Unplug** the dongle from USB
2. **Insert a paperclip** into the pinhole reset button and **hold it down**
3. **Plug in the USB-C cable** while still holding the button
4. **Release** after ~1 second

### Flash via Web Browser (easiest)

1. Download `zigbee_p1_meter.bin` from the [latest release](../../releases/latest)
2. Open the [ESP Web Flasher](https://espressif.github.io/esptool-js/)
3. Put the dongle in flash mode (see above)
4. Click **Connect**, select the serial port, and set the flash address to `0x0`
5. Choose the `.bin` file and click **Program**

<details>
<summary><strong>Flash via command line</strong></summary>

Install esptool (`pip install esptool`) and download `zigbee_p1_meter.bin` from the [latest release](../../releases/latest).

**Windows:**
```
esptool.py --chip esp32c6 --port COM3 --baud 460800 write_flash 0x0 zigbee_p1_meter.bin
```
Find your port in **Device Manager** under **Ports (COM & LPT)**.

**macOS / Linux:**
```bash
esptool.py --chip esp32c6 --port /dev/ttyACM0 --baud 460800 write_flash 0x0 zigbee_p1_meter.bin
```
Find your port with `ls /dev/cu.usb*` (macOS) or `ls /dev/ttyACM*` (Linux).

</details>


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

### OTA Updates

After the initial flash, future updates can be done over-the-air through Zigbee2MQTT using the `.ota` file from the release page.

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

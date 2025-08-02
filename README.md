This software allows remote control of OutEquipPro AC units like the Summit2 using an Arduino microcontroller as a WiFi bridge.

# Installation Instructions

## What You'll Need

- [Wemos C3 Mini](https://www.wemos.cc/en/latest/c3/c3_mini.html)
- [Wemos IR Controller Shield](https://www.wemos.cc/en/latest/d1_mini_shield/ir.html)
- Soldering Iron
- Solder
- Thin Stranded Wire (e.g. 22awg)
- Glue, Zip Tie, or Double-Sided Tape

## Software Installation

> [!CAUTION]
> Do this step _before_ hardware installation.
> This avoids connecting your computer's USB port to a microcontroller being powered externally, as would be the case when powered via the 5V and Ground pins on the control board. Doing so is likely to cause irreversible damage to your computer, microcontroller, and control board.

1. Clone this repo. Open in VS Code with pioarduino.
1. Copy [`src/config.example.h`](blob/main/src/config.example.h) to `src/config.h` and customize as necessary.
1. Copy [`platformio_upload.example.ini`](blob/main/platformio_upload.example.ini) to `platformio_upload.ini` and provide admin credentials for use in future OTA updates.
1. Build and flash the microcontroller firmware using pioarduino.
1. Build and flash the microcontroller filesystem using pioarduino.

> [!NOTE]
> Flashing the firmware and filesystem are separate steps. Be sure to complete both of them.

## Hardware Installation

Attach the IR controller shield to the C3 Mini.

Solder wires onto the control board pads labelled `5V`, `GND`, `RX`, and `TX`. The additional `CAN_RX` and `CAN_TX` pins can be left unpopulated.

Connect the other ends of those wires to the appropriate pins on the microcontroller, as per [`config.h`](blob/main/src/config.example.h). By default, that's:

| Control Board | C3 Mini |
| ------------- | ------- |
| 5V            | VBUS    |
| GND           | GND     |
| RX            | 5       |
| TX            | 4       |

Once hooked up, secure the wires and microcontroller with some combination of glue, a zip tie, or double-sided tape.

> [!NOTE]
> The microcontroller should be affixed in such a location and orientation that the IR shield has one of its LEDs pointed in the direction of the control board. In practice, pointing an LED at the backside of the control board seems to work fine.

> [!IMPORTANT]
> Be sure to affix the microcontroller in order to avoid contact with metal, e.g. A/C mounting brackets.

## Configuration

Supply power to your A/C and join the `OutEquipAC` WiFi network. Your device should detect a captive portal, but if not navigate to `192.168.4.1` to reach the configuration page where you can select the WiFi network that your microcontroller should connect to.

Once WiFi configuration is complete, connect to the microcontroller via your web browser. By default, it should be reachable at `http://outequip-ac.local`.

# How It Works

An Arduino microcontroller (Lolin/Wemos C3 Mini recommended) interfaces directly with the air conditioner control board over a wired serial interface. This enables full bidirectional communcation and control of the air conditioner. In turn, the microcontroller exposes a web app allowing remote control of the air conditioner over WiFi.

## Wired A/C Interface

The control board for this model air conditioner has a wired serial interface. In the case of bluetooth-enabled control boards, this serial interface is populated with a bluetooth module. Otherwise, this interface is unpopulated. With a little bit of solder and some effort, a connection can be made for microcontroller use of this serial interface. Conveniently, the control board exposes 5V and ground pins for powering the microcontroller as well.

From there, the Arduino code manages the A/C control board via [a binary protocol](blob/main/protocol.md). The microcontroller queries the control board for updated state every couple of seconds, and sets that state as needed.

## IR A/C Interface

Unfortunately, due to a bug in how temperature control over serial is handled by the control board, setting temperature must be done over infrared by simulating key presses on the IR remote. In order to confirm that the IR blasting works (and retry on failure), the serial interface is used to query state after IR blasting. Since setting the temperature over IR also sets fan speed, the microcontroller first changes the fan speed, then blasts IR, checking to see if the fan speed has changed back as a result of the IR blast being received. If not, the microcontroller will automatically retry IR blasting up to a limit, and on ultimate failure, reset the fan speed to its original value.

## WiFi Configuration

WiFi is configured at runtime via [NetWizard](https://github.com/ayushsharma82/NetWizard). On start, if there's no WiFi configuration stored, the microcontroller will broadcast a WiFi hotspot with SSID `OutEquipAC` with a captive portal allowing configuration. To reset configuration, reboot the microcontroller 5 times in a row, waiting between 5 and 50 seconds between reboots.

## Web App

A web app offers basic A/C controls (`/` on port 80 once WiFi is configured). The web app updates state from the microcontroller every few seconds.

## API

A JSON state dump can be retrieved at `/var_dump`.

A simple REST API for setting state is available at `/set`. See code for details.

## OTA Updates

A web interface for over-the-air updates is made available via [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) at `/update`.

## Stats Reporting

Statistics can be periodically reported to InfluxDB via UDP. Disabled by default. Configured via `config.h`.

## Debug Interface

A read-write debug interface is made available via [WebSerial](https://github.com/ayushsharma82/WebSerial) at `/webserial`.

A read-only serial log is available via the standard serial interface on the microcontroler.

> [!CAUTION]
> Never connect a USB interface to a microcontroller when powered externally, as would be the case when powered via the 5V and Ground pins on the control board. Doing so is likely to cause irreversible damage to your computer, microcontroller, and control board.

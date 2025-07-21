# Protocol

Baud rate 115200.

To set state, issue a packet with the key and value to set. To query for current state, send a packet with the [key](#keys) being queried, and value being `0`. Wait for a response packet before sending another query or set operation.

# Packet Format

| Length (B) | Datum       | Value                                         |
| ---------- | ----------- | --------------------------------------------- |
| 2          | Preamble    | `0x5a5a`                                      |
| 1          | Length      | `uint8` bytes remaining in this packet        |
| 1          | Device Type | `0x01`                                        |
| 1          | Key         | `uint8`, see [Keys](#keys)                    |
| `Length-5` | Value       | Key-Dependent                                 |
| 1          | Checksum    | `uint8` sum of each preceeding byte in packet |
| 2          | Postamble   | `0x0d0a`                                      |

## Device Type

Device type `0x01` applies to air conditioners.

Other device types appear to be supported, but this lies outside the scope of this project. Source code analysis of [U-Frigo](https://play.google.com/store/apps/details?id=com.kingcontech.ufrigo&hl=en_US), which works atop a very similar wire protocol, revealed other device types including air, water, and dual-mode heaters.

## Keys

| Key | Intent               | R/W | Type     | Value                                             |
| --- | -------------------- | --- | -------- | ------------------------------------------------- |
| 1   | Power                | R/W | `uint8`  | [On/Off Values](#onoff-values)                    |
| 2   | Mode                 | R/W | `uint8`  | [Mode Values](#runmode-values)                    |
| 3   | Set Temperature      | R   | `uint8`  | [Set Temperature Values](#set-temperature-values) |
| 4   | Fan Speed            | R/W | `uint8`  | 1-5                                               |
| 5   | Undervolt Protection | R/W | `uint8`  | decivolts                                         |
| 6   | Overvolt Protection  | R   | `uint8`  | Volts                                             |
| 7   | Intake Air Temp      | R   | `uint8`  | °C                                                |
| 8   | Air Outlet Temp      | R   | `uint8`  | °C                                                |
| 10  | LCD                  | R/W | `uint8`  | [On/Off Values](#onoff-values) TODO: Validate     |
| 16  | Swing                | R/W | `uint8`  | [On/Off Values](#onoff-values)                    |
| 18  | Voltage              | R   | `uint16` | decivolts                                         |
| 19  | Amperage             | R   | `uint16` | Always `0`                                        |
| 28  | Light                | R/W | `uint8`  | [Light Values](#light-values)                     |
| 66  | Active               | R/W | `uint8`  | [See Initialization](#initialization)             |

## Mode Values

| Value | Intent                                                |
| ----- | ----------------------------------------------------- |
| 1     | Cooling                                               |
| 2     | Heating                                               |
| 3     | Fan                                                   |
| 4     | Eco Cooling                                           |
| 5     | Sleep Cooling                                         |
| 6     | Turbo Cooling                                         |
| 7     | Wet Mode TODO: What even is this?! Heat + Cool maybe? |

## On/Off Values

| Value | Intent |
| ----- | ------ |
| 1     | Off    |
| 2     | On     |

## Set Temperature Values

Values are related in °C, within the range 17-30 inclusive.

There is code in the app suggesting that °F values can be used in the range 60-87 inclusive, but emperically this was not found to be the case.

When the control board is set to use °F, values are still reported in °C, rounded down to the nearest whole degree.

> [!IMPORTANT]
> Setting temperature does not work when the unit is running in any mode. Further, the set temperature appears to be overwritten when setting power (`1`) to on (`2`) or when setting mode (`2`).

## Light Values

Light handling in the control board firmware appears to be quite buggy:

1. Though this is an on/off control, the values are opposite to all other on/off controls.
1. When queried, this value returned is almost always 1, regardless of actual state.

| Value | Intent |
| ----- | ------ |
| 1     | On     |
| 2     | Off    |

# Initialization

> [!NOTE]
> This section describes the initialization sequence that the bluetooth module and app perform. It is not necessary and is documented here only for completeness.

The board starts by sending in ASCII an AT command to the bluetooth module.

```
AT+NAME?\r\n
```

The module responds in ASCII with its name.

```
\r\n+NAME:KT2025040004510\r\nOK\r\n
```

From this point on the board and module communicate in packets as described in the [packet format](#packet-format).

The app immediately queries the `Active` (`66`) key on connect. If the control board responds with the value `2`, the app sets the value to `1`.

Following this, keys `2`, `3`, `7`, `8`, `18`, `19` are queried in the app by writing the value `0`, causing the controller to reply with current values.

Finally, the app auto-refreshes values `7`, `8`, `18`, `19` every second or so.

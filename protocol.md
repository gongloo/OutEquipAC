# Protocol

Baud rate 115200.

To set state, issue a packet with the key and value to set. To query for current state, send a packet with the [key](#keys) being queried, and value being `0`. Wait for a response packet before sending another query or set operation.

# Packet Format

| Length (B) | Datum     | Value                                                         |
| ---------- | --------- | ------------------------------------------------------------- |
| 2          | Preamble  | `0x5a5a`                                                      |
| 1          | Length    | `uint8` number of bytes remaining in this packet. Must be >5. |
| 1          | Unknown   | Always `0x01` so far.                                         |
| 1          | Key       | `uint8` See [Keys](#keys).                                    |
| `Length-5` | Value     | Key-Dependent.                                                |
| 1          | Checksum  | `uint8` sum of each preceeding byte in packet.                |
| 2          | Postamble | `0x0d0a`                                                      |

## Keys

| Key | Intent               | Type     | Value                                             |
| --- | -------------------- | -------- | ------------------------------------------------- |
| 1   | Power                | `uint8`  | See [On/Off Values](#onoff-values) TODO: Validate |
| 2   | Mode                 | `uint8`  | See [Mode Values](#runmode-values).               |
| 3   | Set Temperature      | `uint8`  | °C, in range 17-30                                |
| 4   | Fan Speed            | `uint8`  | 1-5                                               |
| 5   | Undervolt Protection | `uint8`  | decivolts                                         |
| 6   | Overvolt Protection  | `uint8`  | Volts                                             |
| 7   | Intake Air Temp      | `uint8`  | °C                                                |
| 8   | Air Outlet Temp      | `uint8`  | °C                                                |
| 10  | LCD                  | `uint8`  | See [On/Off Values](#onoff-values) TODO: Validate |
| 16  | Swing                | `uint8`  | See [On/Off Values](#onoff-values) TODO: Validate |
| 18  | Voltage              | `uint16` | decivolts                                         |
| 19  | Amperage             | `uint16` | deciamps                                          |
| 28  | Light                | `uint8`  | See [On/Off Values](#onoff-values)                |
| 66  | Active               | `uint8`  | Unknown.                                          |

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

# Initialization

This section describes the initialization sequence that the bluetooth module and app perform. It is not necessary and is documented here only for completeness.

The board starts by sending in ASCII an AT command to the bluetooth module.

```
AT+NAME?\r\n
```

The module responds in ASCII with its name.

```
\r\n+NAME:KT2025040004510\r\nOK\r\n
```

From this point on the board and module communicate in packets.

The app immediately sets the `Active` (`66`) key to `1`, apparently in response to the control board sending a value of `2`. This appears to happen at connection start.

Keys `2`, `3`, `7`, `8`, `18`, `19` are auto-refreshed in the app by writing the value `0`, causing the controller to reply with current values.

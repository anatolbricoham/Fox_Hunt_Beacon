# Configuration Guide

The beacon can be configured in three ways:

1. Edit `include/beacon_config.h` before building.
2. Use Serial Monitor commands after flashing.
3. Use the web admin UI from a phone or laptop over WiFi.

The methods overlap but do not expose the same settings. The web UI covers the
beacon controls, WiFi AP, display eco mode, and LoRa network settings. NFC and
MQTT settings are currently available through compile-time defaults or Serial
Monitor commands. Serial and web changes are stored in ESP32 flash and override
compile-time defaults. Run `defaults` in Serial Monitor or click Defaults in the
web UI to restore the values from `include/beacon_config.h`.

The current single-beacon default is `MOE`; fox sync starts that slot without
the extra 60-second delay used by `MOI`. Saved Preferences survive firmware
uploads, so a beacon that still stores `MOI` must run `defaults` or `set fox MOE`
to adopt the single-beacon default.

Boards with a display also have an on-screen settings menu (double-click the
button) for quick on/off toggles. This is a convenience for field use and only
covers a subset of settings — full configuration requires the web UI or serial
monitor.

## Web Admin UI

The beacon hosts a WiFi access point with a captive portal. On boot, look for a
WiFi network named `BricoHams-Fox-XXXX` (last 4 hex of MAC address). Connect to it
from a phone or laptop — the configuration page should auto-open. If it does
not, browse to `http://10.0.0.8/`.

The web UI provides forms for beacon, WiFi AP, display eco mode, and LoRa
settings, plus buttons for test transmission, PTT test, restore defaults, and
reboot. NFC tag lists and MQTT broker credentials must be configured through
Serial Monitor or compile-time defaults. No app or internet connection is needed.

The web UI works on all ESP32 boards. Boards with OLED, TFT, or E-Ink screens
also show status with callsign, fox ID, mode, state, timing, battery, and AP IP
address. E-Ink refresh is slower than OLED/TFT.

The WiFi AP can be turned on or off from the on-screen settings menu or the web
UI. When off, the WiFi radio is disabled to save power.

The AP also has an **auto-off timeout** (default 10 minutes). If no phone or
laptop is connected and no web requests are received for the timeout period, the
AP shuts down automatically to save power. Set the timeout to 0 to disable
auto-off (AP stays on indefinitely). Configure it via the web UI, serial
monitor (`set wifi_ap_timeout 10`), or it defaults to 10 minutes. To turn the AP
back on after auto-off, use the on-screen menu (double-click the button, select
WiFi AP, toggle ON) or the serial command `set wifi_ap on`.

## Recommended Basic Setup

For a standard IARU 5-fox event, the defaults already match the standard cycle
(60 s TX, 240 s idle, warble off, fox sync on). You only need to set the
callsign and fox ID on each beacon:

```text
set call EA5KAO
set fox MOE
```

For a multi-fox ARDF event, use a different fox identifier for each beacon.
With fox sync enabled (the default), the startup delay is calculated
automatically from the fox ID, so all five beacons use the same timing:

| Beacon | Command | Auto startup delay |
| --- | --- | ---: |
| Fox 1 | `set fox MOE` | 0 s |
| Fox 2 | `set fox MOI` | 60 s |
| Fox 3 | `set fox MOS` | 120 s |
| Fox 4 | `set fox MOH` | 180 s |
| Fox 5 | `set fox MO5` | 240 s |

Power all five beacons on at roughly the same time and they will take turns
automatically in the standard round-robin order.

For a finish-line beacon (MO6) on a separate frequency:

```text
set mode beacon
set fox MO6
set beacon_id 60
```

Keep `set call ...` set to the licensed station or club callsign required by
your local rules.

## What The Beacon Transmits

Each transmit window sends:

```text
CALLSIGN in CW -> FOX_ID in CW -> steady carrier (or warble if enabled)
```

With the default settings, the radio sends:

```text
EA5KAO MOE
```

Then it holds a steady carrier until the transmit timer ends. If warble is
enabled, it alternates between 700 Hz and 900 Hz instead.

## Compile-Time Defaults

Edit `include/beacon_config.h` when you want every fresh flash or `defaults`
command to return to your preferred setup.

Settings are stored in ESP32 NVS Preferences and survive firmware uploads. A
new compile-time default does not replace an already-saved value; run `defaults`
after flashing if you want an existing beacon to adopt the current defaults.

| Setting | Meaning | Default |
| --- | --- | --- |
| `DEFAULT_CALLSIGN` | Callsign sent in CW. | `EA5KAO` |
| `DEFAULT_FOX_ID` | ARDF fox identifier sent after the callsign. | `MOE` |
| `DEFAULT_STARTUP_DELAY_SECONDS` | Delay after power-on before the schedule starts. Overridden by fox sync when enabled. | `10` |
| `DEFAULT_TRANSMIT_SECONDS` | Length of each transmit window. | `60` |
| `DEFAULT_IDLE_SECONDS` | Quiet time between transmissions. | `240` |
| `DEFAULT_FOX_SYNC_ENABLED` | Auto-derive startup delay from fox ID for IARU round-robin. | `1` |
| `DEFAULT_BEACON_MODE` | Continuous beacon (MO6 finish) mode. | `0` |
| `DEFAULT_BEACON_ID_INTERVAL_SECONDS` | CW ID repeat interval in continuous beacon mode. | `60` |
| `DEFAULT_CW_WPM` | Morse speed. | `12` |
| `DEFAULT_CW_TONE_HZ` | CW audio tone. | `700` |
| `DEFAULT_WARBLE_ENABLED` | Enables warble after the CW ID. Disabled for IARU standard. | `0` |
| `DEFAULT_WARBLE_LOW_HZ` | Low warble frequency. | `700` |
| `DEFAULT_WARBLE_HIGH_HZ` | High warble frequency. | `900` |
| `DEFAULT_WARBLE_STEP_MS` | Warble step duration. | `350` |
| `DEFAULT_PTT_ACTIVE_LOW` | ESP32 GPIO active level for PTT. | `0` |
| `DEFAULT_PTT_LEAD_MS` | Delay after PTT before audio starts. | `350` |
| `DEFAULT_PTT_TAIL_MS` | Delay before releasing PTT after audio stops. | `350` |
| `DEFAULT_BATTERY_ENABLED` | Enables low-battery cutoff. | `0` |
| `DEFAULT_BATTERY_SCALE` | Battery divider multiplier. | `2.0` |
| `DEFAULT_LOW_BATTERY_VOLTAGE` | Voltage where TX stops. | `3.40` |
| `DEFAULT_NFC_ENABLED` | Enables NFC tag validation. | `0` |
| `DEFAULT_NFC_TAG_WHITELIST` | Semicolon-separated allowed NFC UIDs; empty allows any UID. | `""` |
| `DEFAULT_PARTICIPANT_NAME` | Participant label included in NFC events. | `competitor` |
| `DEFAULT_MQTT_ENABLED` | Enables MQTT event publication. | `0` |
| `DEFAULT_MQTT_BROKER` | MQTT broker hostname. | `broker.local` |
| `DEFAULT_MQTT_PORT` | MQTT broker port. | `1883` |
| `DEFAULT_MQTT_TOPIC` | Base topic for competition events. | `foxhunt/competition` |
| `DEFAULT_WIFI_STATION_SSID` | Station network SSID used to reach the MQTT broker. | `""` |
| `DEFAULT_WIFI_STATION_PASSWORD` | Station network password. | `""` |
| `DEFAULT_MQTT_USER` | Optional MQTT username. | `""` |
| `DEFAULT_MQTT_PASSWORD` | Optional MQTT password. | `""` |
| `DEFAULT_LORA_ENABLED` | Enables the SX127x LoRa network. | `0` |
| `DEFAULT_LORA_MASTER_MODE` | Selects master (`1`) or slave (`0`). | `0` |
| `DEFAULT_LORA_FREQUENCY_HZ` | LoRa frequency in Hz. | `868100000` |
| `DEFAULT_LORA_TX_POWER_DBM` | LoRa transmit power in dBm. | `20` |
| `DEFAULT_LORA_SYNC_WORD` | Network sync word. | `0x12` |
| `DEFAULT_LORA_NODE_ID` | Node ID included in LoRa packets. | `node-01` |

Default pins for classic ESP32 boards:

| Function | GPIO |
| --- | ---: |
| PTT output | 25 |
| Audio output | 26 |
| Status LED | 2 |
| Test button | 0 |
| Battery ADC | 34 |

ESP32-S3 and ESP32-C3 environments override some pins in `platformio.ini`.

## Serial Commands

Open Serial Monitor at 115200 baud and type commands followed by Enter.

| Command | Meaning |
| --- | --- |
| `show` | Print current saved configuration and command list. |
| `test` | Queue one complete beacon transmission. |
| `ptt_test` | Key PTT for about 1.2 seconds with no audio. |
| `defaults` | Restore compile-time defaults and save them to flash. |
| `reboot` | Restart the ESP32. |
| `set call <text>` | Set the CW callsign. |
| `set fox <text>` | Set the ARDF fox ID, such as `MOE` or `MOI`. |
| `set mode fox\|beacon` | Switch between scheduled fox mode and continuous beacon (MO6) mode. |
| `set fox_sync on\|off` | Enable or disable auto startup delay from fox ID. |
| `set beacon_id <seconds>` | Set CW ID repeat interval for continuous beacon mode. |
| `set startup <seconds>` | Set startup hiding delay (overridden by fox sync when enabled). |
| `set tx <seconds>` | Set transmit duration. |
| `set idle <seconds>` | Set quiet time between transmissions. |
| `set wpm <number>` | Set CW speed. |
| `set tone <hz>` | Set CW tone frequency. |
| `set warble on` | Enable warble after the CW ID. |
| `set warble off` | Disable warble after the CW ID. |
| `set warble_low <hz>` | Set low warble frequency. |
| `set warble_high <hz>` | Set high warble frequency. |
| `set warble_step <ms>` | Set how fast the warble alternates. |
| `set lead <ms>` | Set delay after PTT before audio. |
| `set tail <ms>` | Set delay before PTT release after audio. |
| `set ptt active_high` | GPIO HIGH keys the interface. |
| `set ptt active_low` | GPIO LOW keys the interface. |
| `set battery on` | Enable low-battery cutoff. |
| `set battery off` | Disable low-battery cutoff. |
| `set battery_scale <number>` | Set ADC divider multiplier. |
| `set low_battery <volts>` | Set low-battery cutoff voltage. |
| `set wifi_ap on\|off` | Turn the WiFi AP and web admin UI on or off. |
| `set wifi_ap_timeout <minutes>` | Auto-off the AP after this many idle minutes (0 = never). |
| `set eco_mode on\|off` | Enable or disable display eco mode (screen off after 4 s). |
| `set nfc on\|off` | Enable or disable NFC tag reading. |
| `set nfc_tags <uid;uid>` | Set the allowed NFC UIDs; an empty list allows any UID. |
| `set participant_name <name>` | Set the participant label sent with a validation event. |
| `set mqtt on\|off` | Enable or disable MQTT publication. |
| `set mqtt_broker <host>` | Set the MQTT broker hostname. |
| `set mqtt_port <port>` | Set the MQTT broker port. |
| `set mqtt_topic <topic>` | Set the base MQTT topic. Validated events use `<topic>/validated`. |
| `set wifi_ssid <ssid>` | Set the WiFi station SSID used for MQTT connectivity. |
| `set wifi_password <password>` | Set the WiFi station password. |
| `set mqtt_user <user>` | Set the MQTT username. |
| `set mqtt_password <password>` | Set the MQTT password. |
| `set lora on\|off` | Enable or disable the LoRa network. |
| `set lora_role master\|slave` | Set this node's network role. |
| `set lora_node <id>` | Set this node's unique ID. |
| `set lora_frequency <hz>` | Set the LoRa frequency in Hz (860-930 MHz). |
| `set lora_power <dbm>` | Set transmit power (2-20 dBm). |
| `set lora_sync_word <0-255>` | Set the network sync word in decimal; must match on all nodes. |

The firmware constrains values to practical ranges. If a value is outside the
allowed range, it is clipped to the nearest allowed value.

## LoRa Network Settings

Use the web admin's **LoRa Network** section or the serial commands below to
configure the optional SX127x LoRa radio. Configure one node as master and the
others as slaves; all nodes must use the same frequency and sync word. LoRa
settings are saved immediately but the radio is initialized at boot, so reboot
after changing them. Frequency and transmit power must comply with local
regulations and the selected radio module's supported band.

```text
set lora on
set lora_role master
set lora_node master-01
set lora_frequency 868100000
set lora_power 20
set lora_sync_word 18
```

The slave uses `set lora_role slave` and a unique node ID. Supported ranges are
860-930 MHz, 2-20 dBm, and sync word 0-255. The default sync word is decimal 18
(`0x12`). Confirm the board-specific LoRa SPI pins before connecting hardware.
The master distributes network uptime, not UTC; absolute time and GPS/NTP
integration are not implemented.

## Timing Examples

Standard IARU 5-fox event (the default):

```text
set fox_sync on
set tx 60
set idle 240
set warble off
```

Simple practice beacon with faster cycling:

```text
set fox_sync off
set startup 60
set tx 20
set idle 60
set warble on
```

Classic two-minute training cycle:

```text
set fox_sync off
set startup 300
set tx 30
set idle 90
```

Longer low-power training cycle:

```text
set fox_sync off
set startup 300
set tx 15
set idle 165
```

Finish-line continuous beacon (MO6) on a separate frequency:

```text
set mode beacon
set fox MO6
set beacon_id 60
```

## Audio Setup

Start with:

```text
set wpm 12
set tone 700
set warble_low 700
set warble_high 900
set warble_step 350
```

If the received audio sounds harsh, distorted, or too wide, lower the physical
audio level going into the radio microphone input first. After that, try a
slower warble or lower tone frequencies.

## PTT Setup

For the recommended transistor, MOSFET, or optocoupler interface where GPIO HIGH
keys the radio:

```text
set ptt active_high
```

If your circuit keys the radio when the ESP32 output is LOW:

```text
set ptt active_low
```

Use `ptt_test` before connecting audio. The radio should key and then release
reliably every time.

## Battery Setup

Leave battery monitoring disabled until a voltage divider is installed and
calibrated:

```text
set battery off
```

After building the divider:

```text
set battery_scale 2.0
set low_battery 3.4
set battery on
show
```

Compare the voltage printed by `show` with a multimeter and adjust
`battery_scale` until it is close enough for field use.

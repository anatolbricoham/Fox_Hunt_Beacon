# BricoHams ESP32 Fox Hunt Beacon

<p align="center">
  <img src="images/bricohams_horizontal.png" alt="BricoHams amateur radio and DIY logo" width="600">
</p>

ESP32 firmware that turns a cheap handheld walkie-talkie into an amateur radio
fox hunting (ARDF) beacon. The ESP32 handles timing, CW identification, audio
tones, push-to-talk, battery monitoring and configuration. The radio generates
the RF signal.

- **32 ESP32 boards** supported, including Heltec and LilyGO/TTGO boards with
  OLED, TFT or E-Ink screens.
- **IARU 5-fox timing** out of the box: 60 s TX, 240 s idle, automatic
  round-robin slots from the fox ID.
- **No-install setup**: flash from the browser, then configure from a phone
  over the beacon's own WiFi access point.

> [!IMPORTANT]
> Transmit only on frequencies, modes and power levels your licence and local
> rules allow, and identify with your own callsign. The firmware ships with
> `EA5KAO` as the default callsign; change it before transmitting.

## Contents

- [Quick Start](#quick-start)
- [What The Beacon Sends](#what-the-beacon-sends)
- [Hardware](#hardware)
- [Configuration](#configuration)
- [Supported Boards](#supported-boards)
- [Features](#features)
- [Building From Source](#building-from-source)
- [Documentation](#documentation)
- [Acknowledgments And License](#acknowledgments-and-license)

## Quick Start

### 1. Flash the firmware

**Option A: web flasher (no tools needed):** <https://fox.hamradio.my>

1. Open the page in Chrome, Edge or Opera (Web Serial is required; Firefox and
   Safari do not work).
2. Pick your board, click **Install Firmware**, and choose the USB port.
3. Use a data-capable USB cable and install the CP2102/CH340/FT232 driver your
   board needs.

The web flasher serves binaries from the latest GitHub Release. If no Release
has been published yet, use option B.

**Option B: PlatformIO:** see [Building From Source](#building-from-source).

### 2. Configure it

After boot, the beacon starts an open WiFi network called
`BricoHams-Fox-XXXX`. Connect from a phone or laptop; the captive portal should
open automatically. If it does not, browse to <http://10.0.0.8/>.

Set at least:

- **Callsign:** your licensed callsign.
- **Fox ID:** `MOE`, `MOI`, `MOS`, `MOH` or `MO5`.
- **PTT polarity:** `active_high` for the recommended transistor/opto
  interface.

Settings are saved to flash and survive reboots and firmware updates.

### 3. Bench test before connecting a radio

1. Run **PTT test** (web UI or `ptt_test` over serial) and check the PTT pin
   with an LED or multimeter.
2. Wire the PTT interface to the radio and repeat the PTT test.
3. Add the audio wiring with the level trimpot turned low, run **Test**, and
   listen on a second receiver.
4. Test into a dummy load or at the lowest power before going to the field.

Then work through the [Field Checklist](docs/field-checklist.md).

## What The Beacon Sends

Each transmit window:

```text
PTT on -> lead delay -> CALLSIGN (CW) -> gap -> FOX ID (CW) -> steady carrier -> tail delay -> PTT off
```

With the defaults, fox 1 sends `EA5KAO MOE` in Morse at 12 WPM / 700 Hz, then
holds a carrier until 60 s have passed. It is then quiet for 240 s. An optional
700/900 Hz warble can replace the carrier for training.

With **fox sync** on (the default), the startup delay is derived from the fox
ID, so five beacons switched on together fall into the standard round-robin on
one frequency:

| Fox | ID | Transmits at |
| --- | --- | --- |
| 1 | `MOE` | 0:00 – 1:00 |
| 2 | `MOI` | 1:00 – 2:00 |
| 3 | `MOS` | 2:00 – 3:00 |
| 4 | `MOH` | 3:00 – 4:00 |
| 5 | `MO5` | 4:00 – 5:00 |

**Beacon mode** (`set mode beacon`) instead keys continuously and repeats the ID
every 60 s. It is intended for an MO6 finish beacon on a separate frequency.

For ARDF background and the IARU event standard, see
[Understanding The Beacon](docs/understanding.md).

## Hardware

Typical build:

- ESP32 board from the [supported list](#supported-boards).
- Cheap handheld radio plus a speaker-mic plug/cable.
- NPN transistor, MOSFET or optocoupler for PTT.
- RC filter, coupling capacitor and trimpot for audio into the mic input.
- USB power bank or Li-ion/LiFePO4 pack; optional resistor divider for battery
  sensing.

Signal flow (pins shown are for classic ESP32 DevKit boards):

```text
PTT     GPIO 25 -> transistor / opto        -> radio PTT
Audio   GPIO 26 -> RC filter + trimpot + cap -> radio mic input
Button  GPIO 0  -> test transmission / on-screen menu
LED     GPIO 2  -> status LED
Battery GPIO 34 <- resistor divider          <- battery (disabled by default)
```

> [!NOTE]
> Pins differ per board. Check the [Board And Pin Matrix](docs/board-matrix.md)
> for your environment, or run `show` over serial to print the pins in use.
> Only connect 3.3 V logic to ESP32 GPIOs.

The firmware can also drive an SA818/DRA818 FM module or a full amateur
handheld through the same PTT and audio lines. It does **not** use the onboard
LoRa radio on Heltec/LilyGO boards as an FM transmitter.

Wiring details: [Wiring Guide](docs/wiring.md). Full parts list with values:
[Component Reference](docs/components.md).

## Configuration

You can configure the beacon in three ways. All of them save to the same flash
settings.

| Method | Needs | Can change |
| --- | --- | --- |
| Web admin UI at `http://10.0.0.8/` | Phone or laptop on the beacon's WiFi | Everything, plus test, PTT test, defaults and reboot |
| Serial Monitor, 115200 baud | USB cable | Everything |
| On-screen menu | Board with a display | On/off toggles only |

Common serial commands:

```text
show                 print config, pins, battery
test                 send one transmission now
ptt_test             key PTT ~1.2 s with no audio
set call EA5KAO      callsign
set fox MOI          fox ID
set tx 60            transmit seconds
set idle 240         idle seconds
set ptt active_high  PTT polarity
defaults             restore compile-time defaults
```

For the full command list, value ranges, audio/PTT/battery setup and timing
examples, see the [Configuration Guide](docs/configuration.md).

**WiFi AP:** the AP has no password. It turns itself off after 10 minutes with
no clients (`set wifi_ap_timeout 0` keeps it on). Re-enable it from the
on-screen menu or with `set wifi_ap on`.

**On-screen menu (display boards):** double-click the button to open it. Single
click moves to the next item, double click toggles it, and a 2 s hold exits. It
toggles WiFi AP, warble, fox sync, battery, fox/beacon mode and display eco
mode.

**Display eco mode** is on by default. The screen switches off after 4 s, and
any button press wakes it.

## Supported Boards

| Family | PlatformIO environments | Display |
| --- | --- | --- |
| Generic | `esp32dev`, `esp32doit-devkit-v1`, `lolin32`, `esp32-s3-devkitc-1`, `esp32-c3-devkitm-1` | None |
| Heltec OLED | `heltec-wifi-kit-32` (+`-v2`, `-v3`), `heltec-wifi-lora-32` (+`-v2`, `-v3`), `heltec-wireless-stick`, `heltec-wireless-stick-lite` (+`-v3`) | SSD1306 128x64 |
| Heltec TFT | `heltec-wireless-tracker`, `heltec-vision-master-t190` | ST7735 / ST7789 |
| Heltec E-Ink | `heltec-wireless-paper`, `heltec-vision-master-e213`, `heltec-vision-master-e290` | SSD1680 |
| Heltec, no display | `heltec-capsule-sensor-v3` | None |
| TTGO OLED | `ttgo-lora32-v1`, `ttgo-lora32-v2`, `ttgo-lora32-v21`, `ttgo-t-beam` | SSD1306 128x64 |
| LilyGO TFT | `lilygo-t-display`, `lilygo-t-display-s3`, `ttgo-t-watch` | ST7789 |
| LilyGO, no display | `lilygo-t3-s3`, `ttgo-t-oi-plus`, `ttgo-t1`, `ttgo-t7-v13-mini32`, `ttgo-t7-v14-mini32` | None |

Pick the environment that matches your exact board and revision. Some Heltec
S3 boards are compatibility builds; see [Heltec](docs/heltec-boards.md) and
[LilyGO](docs/lilygo-boards.md) notes. For an unlisted classic ESP32 board,
start with `esp32dev`.

## Features

- Callsign and ARDF fox ID in CW, followed by a steady carrier (IARU format) or
  optional warble.
- IARU 5-fox defaults with automatic fox-slot synchronization and a drift-free
  schedule (a test transmission does not move the slot).
- Continuous beacon mode for an MO6 finish transmitter.
- PTT lead/tail guard times, selectable polarity and a PTT-only test.
- Optional Li-ion battery monitor with voltage, percentage and low-battery
  cutoff that resumes automatically when the battery recovers.
- WiFi captive-portal web admin with an idle auto-off timeout.
- Live status screen on OLED, TFT and E-Ink boards, with state colours or
  patterns and an eco mode.
- Status LED and test button on every board.
- Settings in flash, with sane-range clamping and a `defaults` reset.

Planned: Bluetooth serial configuration, deep sleep between transmissions, and
multiple fox profiles.

## Building From Source

Requirements: [PlatformIO](https://platformio.org/) (VS Code extension or
`pip install platformio`) and a data-capable USB cable.

```sh
git clone https://github.com/anatolbricoham/Fox_Hunt_Beacon.git
cd Fox_Hunt_Beacon

pio run -e esp32dev                 # build
pio run -e esp32dev -t upload       # flash (hold BOOT if it will not connect)
pio device monitor -b 115200        # serial console
```

Replace `esp32dev` with your board's environment. To change the compiled-in
defaults (callsign, fox ID, timing, PTT, battery), edit
[include/beacon_config.h](include/beacon_config.h). Saved settings on an
already-flashed board take priority until you run `defaults`.

Step-by-step VS Code instructions are in the
[Installation Guide](docs/installation.md).

### Project layout

```text
src/main.cpp            beacon state machine, CW/audio, PTT, serial commands
src/web_admin.cpp       WiFi AP, captive portal and JSON API
src/display.cpp         OLED / TFT / E-Ink status screen and menu
include/beacon_config.h compile-time defaults and default pins
data_embed/             web UI source (HTML/CSS/JS)
include/beacon_runtime.h  thread-safe bridge between web server task and main loop
tools/compress.py       gzips data_embed/ into include/web_assets.h at build time
tools/merge_bin.py      builds firmware-merged.bin (bootloader + app) for the web flasher
platformio.ini          one environment per supported board
flasher/                browser web flasher (GitHub Pages)
docs/                   user and developer guides
```

`include/web_assets.h` is generated, so edit `data_embed/` instead.

### Releases

CI builds all 32 environments on pushes and pull requests that touch firmware
or build files. To publish firmware for the web
flasher, push a `v*` tag (for example `v1.1.0`). The release workflow attaches
two files per board: `firmware-<env>.bin`, a merged factory image flashed at
offset 0 (what the web flasher uses), and `firmware-<env>-app.bin`, the
application alone for offset 0x10000. The Pages workflow then redeploys the
flasher with those binaries. When you add a board, update `platformio.ini`, the
workflow matrices, `flasher/manifest.json` and the flasher board selector
together. See [Software Architecture](docs/software-architecture.md).

## Documentation

| Guide | Covers |
| --- | --- |
| [Understanding The Beacon](docs/understanding.md) | ARDF basics, IARU timing, signal format, hardware roles |
| [Installation](docs/installation.md) | PlatformIO / VS Code build, upload and first bench test |
| [Configuration](docs/configuration.md) | Every setting and serial command; audio, PTT and battery setup |
| [Wiring](docs/wiring.md) | PTT and audio interface for cheap handheld radios |
| [Board And Pin Matrix](docs/board-matrix.md) | All environments, GPIO profiles and display buses |
| [Heltec Boards](docs/heltec-boards.md) / [LilyGO Boards](docs/lilygo-boards.md) | Board-specific pin notes |
| [Field Checklist](docs/field-checklist.md) | Bench, radio, power, deployment and recovery checks |
| [Troubleshooting](docs/troubleshooting.md) | Flashing, serial, web UI, PTT, audio, timing, battery and RF problems |
| [Component Reference](docs/components.md) | Every hardware part (BOM, values) and every firmware/tooling file |
| [Software Architecture](docs/software-architecture.md) | Boot sequence, state machine, threading, persistence, web API, CI and releases |

## Acknowledgments And License

This project continues the original
[9M2PJU ESP32 Fox Hunt Beacon](https://github.com/9M2PJU/9M2PJU-ESP32-Fox-Hunt-Beacon)
and is maintained by BricoHams. See [ACKNOWLEDGMENTS.md](ACKNOWLEDGMENTS.md).

Licensed under the GNU General Public License v3.0 or later
(`GPL-3.0-or-later`). See [LICENSE](LICENSE).

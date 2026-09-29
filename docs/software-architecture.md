# Software Architecture And Workflows

This document describes the firmware from source to running beacon. It is for contributors and operators who need to understand where a behavior is implemented, how configuration moves through the system, and how firmware reaches the browser flasher.

## Source Map

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | Boot sequence, beacon state machine, Morse/audio generation, PTT, battery checks, serial commands, physical button and on-screen menu. |
| `include/beacon_config.h` | Compile-time defaults, fallback GPIOs, configuration structure, state enumeration and GPIO uniqueness checks. |
| `platformio.ini` | 32 board environments, shared board profiles, display buses, GPIO overrides, libraries and build flags. |
| `src/display.cpp` | Display abstraction and OLED, TFT, E-Ink implementations; no-display builds compile to no-op functions. |
| `include/display_config.h` | Display-type selection and fallback display flags. |
| `src/web_admin.cpp`, `include/web_admin.h` | Wi-Fi AP, captive DNS, HTTP routes, JSON configuration API and auto-off timer. |
| `data_embed/index.html`, `data_embed/style.css`, `data_embed/script.js` | Source files for the embedded browser configuration UI. |
| `tools/compress.py` | Pre-build gzip compression and generation of `include/web_assets.h`. Do not edit the generated header directly. |
| `.github/workflows/build.yml` | CI build matrix for all 32 PlatformIO environments. |
| `.github/workflows/release.yml` | Tagged firmware builds and GitHub Release asset publication. |
| `.github/workflows/pages.yml` | GitHub Pages deployment of the web flasher and firmware assets from the latest Release. |

## Boot Sequence

At reset, `setup()` in `src/main.cpp` performs these steps:

1. Configure PTT and LED GPIOs as outputs and force PTT inactive before doing other work.
2. Start Serial at 115200 baud.
3. Load compile-time defaults, then overlay saved values from ESP32 Preferences namespace `foxbeacon`.
4. Normalize saved callsign and fox ID and clamp numeric values to the supported ranges.
5. Initialize the selected display, LEDC audio output and optional battery ADC.
6. Start the Wi-Fi AP and web admin server when `wifiApEnabled` is true.
7. Resolve the startup delay and enter `StartupDelay`, `Transmitting`, `Idle`, or `ContinuousTransmit` as appropriate.

Compile-time defaults are only used when a preference key has not been saved. Use the serial `defaults` command or web Defaults action to replace saved preferences with the current compile-time defaults. A firmware upload alone does not erase or replace Preferences.

The standard single-beacon defaults are callsign `EA5KAO`, fox ID `MOE`, TX 60 seconds, idle 240 seconds, fox sync enabled, warble disabled, Wi-Fi AP enabled and display eco mode enabled. With fox sync enabled, standard ID `MOE` resolves to a zero-second slot delay and begins the first TX slot immediately. `MOI`, `MOS`, `MOH` and `MO5` resolve to 1, 2, 3 and 4 TX slots of startup delay respectively. For a nonstandard ID, the manual startup delay is used.

## Beacon State Machine

The firmware state is one of:

| State | Behavior | Exit condition |
| --- | --- | --- |
| `StartupDelay` | PTT remains off; status LED shows startup activity. | Resolved startup delay expires. |
| `Idle` | PTT/audio are off; idle LED pattern runs. | `idleSeconds` expires, test is queued, or continuous mode is enabled. |
| `Transmitting` | Runs one scheduled TX window, then returns to `Idle`. | TX routine completes or low-battery handling intervenes. |
| `ContinuousTransmit` | Holds PTT, sends ID at `beaconIdIntervalSeconds`, and services button, serial, web and battery work between IDs. | Continuous mode is disabled or low battery is detected. |
| `LowBatteryHalt` | PTT/audio stay off and the low-battery LED pattern runs. | Reboot or operator intervention. |

Normal fox mode follows:

```text
StartupDelay -> Transmitting -> Idle -> Transmitting -> Idle -> ...
```

When fox sync is disabled or the fox ID is not one of the standard five, the startup delay ends in `Idle`, which then waits for the configured idle time before the first scheduled transmission. A queued `test` bypasses the current wait once. Continuous mode follows its startup delay, if configured, and then remains in `ContinuousTransmit`.

Changing modes from web or serial is applied by the main loop. Enabling continuous mode during startup does not bypass the startup delay. Disabling it causes the continuous loop to release PTT and return to scheduled operation.

## Scheduled Transmission

A scheduled TX window does the following:

1. Capture the TX deadline and assert PTT using the configured active GPIO polarity.
2. Wait `pttLeadMs` for the radio to settle.
3. Generate the configured callsign and fox ID as Morse audio using LEDC PWM.
4. With warble enabled, alternate the two configured audio tones until the audio deadline. With warble disabled, stop audio after the ID and keep PTT asserted for a steady RF carrier.
5. Reserve `pttTailMs` inside the TX window, then release PTT at the deadline.

The TX duration is measured from PTT assertion and includes the lead and tail times. Morse text is always completed; an unusually long callsign/ID at a low WPM can therefore overrun the nominal deadline. Keep identifiers concise and validate the on-air duration for custom settings.

Continuous mode keeps PTT asserted between IDs and schedules the next ID from the start of the current ID interval. It sends CW only; the optional fox-mode warble setting does not modulate continuous mode.

## Configuration And Persistence

Configuration has three entry points:

1. Compile-time macros in `include/beacon_config.h` establish defaults for new settings and the `defaults` operation.
2. Serial commands use `handleCommand()` and `handleSetCommand()` in `src/main.cpp`.
3. The web admin UI submits form fields to `POST /api/config` in `src/web_admin.cpp`.

Serial and web updates are persisted with ESP32 Preferences. `GET /api/config` serializes current values and state for the browser UI. Numerical inputs are constrained before they are saved. The web and serial controls expose callsign, fox ID, mode, fox sync, startup/TX/idle timing, CW, warble, PTT polarity and guards, battery, Wi-Fi AP timeout, and display eco mode.

Serial Monitor runs at 115200 baud. Common commands:

```text
show
set call EA5KAO
set fox MOE
set fox_sync on
set tx 60
set idle 240
test
ptt_test
defaults
reboot
```

The on-screen menu is a convenience subset. In the status screen, a single-click queues a test transmission; a double-click opens the menu. In the menu, single-click advances, double-click toggles the selected item, and a long press exits. OneButton debounces and recognizes the gestures. A queued test in continuous mode resends the beacon ID without releasing PTT; a PTT-only test restores the prior keyed state.

## Wi-Fi Web Admin

When enabled, the device starts an open AP named `BricoHams-Fox-XXXX`, where the suffix comes from the chip MAC address. The AP uses `10.0.0.8`; captive DNS redirects requests to the local web server. The UI is available at `http://10.0.0.8/` without an internet connection.

| Route | Method | Purpose |
| --- | --- | --- |
| `/` | GET | Return embedded HTML. |
| `/style.css` | GET | Return embedded CSS. |
| `/script.js` | GET | Return embedded JavaScript. |
| `/api/config` | GET | Read current configuration and runtime status. |
| `/api/config` | POST | Validate, update and save settings. |
| `/api/test` | POST | Queue one test transmission. |
| `/api/ptt_test` | POST | Run the PTT-only test. |
| `/api/defaults` | POST | Restore compile-time defaults and save them. |
| `/api/reboot` | POST | Restart the ESP32. |

The AP auto-stops after `wifiApTimeoutMinutes` without a connected station or web activity; `0` disables the timeout. It can be re-enabled from the display menu or Serial Monitor if those controls are available.

## Displays And Power

`DISPLAY_TYPE` selects the display implementation: `0` no display, `1` SSD1306 OLED/U8g2, `2` TFT/TFT_eSPI, or `3` E-Ink/GxEPD2. Board-specific pins and libraries are supplied by the matching PlatformIO environment.

Display updates are throttled; status is refreshed about once per second and the interactive menu at most five times per second. Eco mode sleeps the display after four seconds without interaction, with a ten-second boot grace period. OLED uses controller power-save, TFT switches the configured backlight, and E-Ink powers down VEXT and reinitializes the panel with a full refresh after wake. Slow E-Ink refresh is expected and is independent of the beacon state machine.

## Battery Monitoring

Battery measurement is disabled by default. When enabled, firmware samples the selected ADC pin, converts millivolts using `batteryScale`, estimates a single-cell Li-ion percentage and compares voltage with `lowBatteryVoltage`. A low reading moves the state machine to `LowBatteryHalt` and forces audio/PTT off. Install and calibrate the resistor divider before enabling it; never allow an ESP32 ADC input to exceed its permitted voltage.

## Build And Generated Web Assets

The default PlatformIO target is `esp32dev`:

```sh
pio run
```

Select another exact environment for other hardware:

```sh
pio run -e heltec-wifi-lora-32-v3
pio run -e heltec-wireless-paper
```

Before compiling, `extra_scripts = pre:tools/compress.py` gzip-compresses the three `data_embed/` files with a fixed timestamp and creates `include/web_assets.h`. If the generated bytes are unchanged, the script leaves the header timestamp alone so PlatformIO can reuse incremental build outputs. The firmware serves these arrays with `Content-Encoding: gzip`; no SPIFFS/LittleFS upload is required for the web UI.

`include/web_assets.h` is generated. Edit the files in `data_embed/` and let PlatformIO regenerate the header.

## CI, Release, And Web Flasher

1. Pushes and pull requests that touch firmware/build inputs run `.github/workflows/build.yml`, which compiles the 32 environments listed in the workflow matrix.
2. To publish firmware, create and push a version tag matching `v*` (or manually run the release workflow with an existing tag). The release workflow builds every environment, names each binary `firmware-<environment>.bin`, and attaches all 32 binaries to the GitHub Release.
3. The Pages workflow deploys `flasher/` and downloads the firmware assets from the latest GitHub Release into `flasher/firmware/`.
4. `flasher/manifest.json` and the board selector in `flasher/index.html` map a board choice to the corresponding Release binary.

Without a GitHub Release, the Pages workflow deliberately deploys the flasher without firmware files; the browser page can load but cannot install a build until a Release exists. Keep the PlatformIO environment names, CI/release matrices, manifest build names and selector entries synchronized when adding or removing a board.

## Safe Hardware Bring-Up

1. Build and upload the exact board environment with the radio disconnected.
2. Open Serial Monitor at 115200 baud and run `show`.
3. Run `ptt_test` and confirm the assigned GPIO with a meter or LED.
4. Verify the isolated PTT circuit keys and releases before connecting radio audio.
5. Add audio through an appropriate coupling/attenuation circuit with the level low.
6. Test at minimum practical power or into a suitable dummy load.

A successful PlatformIO build validates source and board configuration, not the wiring or RF behavior of a specific board revision. See [Board And Pin Matrix](board-matrix.md), [Heltec Board Support](heltec-boards.md), [LilyGO Board Support](lilygo-boards.md), and [Wiring Guide](wiring.md).

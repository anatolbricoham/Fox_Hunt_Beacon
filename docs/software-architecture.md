# Software Architecture And Workflows

This document describes the firmware from source to running beacon. It is for contributors and operators who need to understand where a behavior is implemented, how configuration moves through the system, and how firmware reaches the browser flasher.

## Source Map

| Path | Responsibility |
| --- | --- |
| `src/main.cpp` | Boot sequence, beacon state machine, Morse/audio generation, PTT, battery checks, serial commands, physical button, on-screen menu and the cross-task request queue. |
| `src/competition.cpp`, `include/competition.h` | RC522 NFC validation, participant allowlist, MQTT publication, and a Preferences-backed offline MQTT queue. |
| `src/lora_network.cpp`, `include/lora_network.h` | SX127x LoRa master/slave packets, network uptime sync, NFC event forwarding, ACK/retry handling, and duplicate suppression. |
| `include/beacon_runtime.h` | Thread-safe interface between the main loop and the async web server task (config snapshot, queued config/actions, cached status). |
| `include/beacon_config.h` | Compile-time defaults, fallback GPIOs, NFC/MQTT/LoRa configuration, state enumeration and GPIO uniqueness checks. |
| `platformio.ini` | 32 board environments, shared board profiles, display buses, GPIO overrides, libraries and build flags. |
| `src/display.cpp` | Display abstraction and OLED, TFT, E-Ink implementations; no-display builds compile to no-op functions. |
| `include/display_config.h` | Display-type selection and fallback display flags. |
| `src/web_admin.cpp`, `include/web_admin.h` | Wi-Fi AP, captive DNS, HTTP routes, JSON configuration API and auto-off timer. |
| `data_embed/index.html`, `data_embed/style.css`, `data_embed/script.js` | Source files for the embedded browser configuration UI. |
| `tools/compress.py` | Pre-build gzip compression and generation of `include/web_assets.h`. Do not edit the generated header directly. |
| `tools/merge_bin.py` | Post-build creation of `firmware-merged.bin` (bootloader + partitions + boot_app0 + app) for flashing at offset 0. |
| `.github/workflows/build.yml` | CI build matrix for all 32 PlatformIO environments. |
| `.github/workflows/release.yml` | Tagged firmware builds and GitHub Release asset publication. |
| `.github/workflows/pages.yml` | GitHub Pages deployment of the web flasher and firmware assets from the latest Release. |

## Boot Sequence

At reset, `setup()` in `src/main.cpp` performs these steps:

1. Create the configuration mutex.
2. Load compile-time defaults, then overlay saved values from ESP32 Preferences namespace `foxbeacon`; normalize the callsign and fox ID and clamp numeric values to the supported ranges.
3. Drive the PTT pin to its inactive level for the saved polarity, then make it an output. Loading first matters: with `active_low` wiring, an output that starts LOW would key the radio during boot.
4. Configure the LED and button, then start Serial at 115200 baud.
5. Initialize the selected display, LEDC audio output and battery ADC attenuation.
6. Start the Wi-Fi AP and web admin server when `wifiApEnabled` is true.
7. Initialize the SX127x LoRa radio when enabled, then initialize the NFC reader and MQTT client when competition settings require them.
8. Print the active configuration and resolve the startup delay before entering `StartupDelay`, `Transmitting`, `Idle`, or `ContinuousTransmit` as appropriate.

Compile-time defaults are only used when a preference key has not been saved. Use the serial `defaults` command or web Defaults action to replace saved preferences with the current compile-time defaults. A firmware upload alone does not erase or replace Preferences.

The standard single-beacon defaults are callsign `EA5KAO`, fox ID `MOE`, TX 60 seconds, idle 240 seconds, fox sync enabled, warble disabled, Wi-Fi AP enabled and display eco mode enabled. With fox sync enabled, standard ID `MOE` resolves to a zero-second slot delay and begins the first TX slot immediately. `MOI`, `MOS`, `MOH` and `MO5` resolve to 1, 2, 3 and 4 TX slots of startup delay respectively. For a nonstandard ID, the manual startup delay is used.

## Beacon State Machine

The firmware state is one of:

| State | Behavior | Exit condition |
| --- | --- | --- |
| `StartupDelay` | PTT remains off; status LED shows startup activity. | Resolved startup delay expires. |
| `Idle` | PTT/audio are off; idle LED pattern runs. | The next scheduled slot (`nextTxAt`) is reached, a test is queued, or continuous mode is enabled. |
| `Transmitting` | Runs one scheduled TX window, then returns to `Idle`. | TX routine completes or low-battery handling intervenes. |
| `ContinuousTransmit` | Holds PTT, sends ID at `beaconIdIntervalSeconds`, and services button, serial, web and battery work between IDs. | Continuous mode is disabled or low battery is detected. |
| `LowBatteryHalt` | PTT/audio stay off and the low-battery LED pattern runs. | Battery monitoring is disabled, or the voltage rises 0.2 V above the cutoff. The beacon then resumes in its original slot. |

Normal fox mode follows:

```text
StartupDelay -> Transmitting -> Idle -> Transmitting -> Idle -> ...
```

When fox sync is disabled or the fox ID is not one of the standard five, the startup delay ends in `Idle`, which then waits for the configured idle time before the first scheduled transmission. Continuous mode follows its startup delay, if configured, and then remains in `ContinuousTransmit`.

Scheduled transmissions use an absolute timeline. `nextTxAt` holds the start time of the next slot and advances by exactly `(tx + idle)` seconds per cycle, measured from the ideal slot start rather than from when the loop noticed it. Loop latency therefore does not accumulate, and five foxes started together stay in their round-robin slots for the whole event.

A queued `test` transmits immediately without moving the schedule. If the test overlaps the next slot, that slot is skipped (the fox has just transmitted) and the following one is kept. A test during `StartupDelay` does not restart the startup countdown.

Changing modes from web or serial is applied by the main loop. Enabling continuous mode during startup does not bypass the startup delay. Disabling it causes the continuous loop to release PTT and return to scheduled operation.

## Scheduled Transmission

A scheduled TX window does the following:

1. Capture the TX deadline and assert PTT using the configured active GPIO polarity.
2. Wait `pttLeadMs` for the radio to settle.
3. Generate the configured callsign and fox ID as Morse audio using LEDC PWM.
4. With warble enabled, alternate the two configured audio tones until the audio deadline. With warble disabled, stop audio after the ID and keep PTT asserted for a steady RF carrier.
5. Reserve `pttTailMs` inside the TX window, then release PTT at the deadline.

During the steady-carrier part of the window (no audio), the firmware keeps servicing the button, Serial, web admin, queued web requests and the display, so the screen shows `TX` and the captive portal keeps answering. This servicing stops 1.5 s before the deadline so a slow display refresh cannot stretch the TX window. CW and warble generation are never interrupted.

The TX duration is measured from PTT assertion and includes the lead and tail times. Morse text is always completed; an unusually long callsign/ID at a low WPM can therefore overrun the nominal deadline. Keep identifiers concise and validate the on-air duration for custom settings.

Continuous mode keeps PTT asserted between IDs and schedules the next ID from the start of the current ID interval. It sends CW only; the optional fox-mode warble setting does not modulate continuous mode.

## Configuration And Persistence

Configuration has three entry points:

1. Compile-time macros in `include/beacon_config.h` establish defaults for new settings and the `defaults` operation.
2. Serial commands use `handleCommand()` and `handleSetCommand()` in `src/main.cpp`.
3. The web admin UI submits form fields to `POST /api/config` in `src/web_admin.cpp`.

Serial and web updates are persisted with ESP32 Preferences. `GET /api/config` serializes current values and state for the browser UI. Numerical inputs are constrained and the callsign/fox ID are normalized (uppercase `A-Z`, `0-9`, `/`, `-`) before they are saved, whichever entry point is used. A POST only changes the fields it contains. The web UI includes beacon, AP/display, and LoRa settings; NFC and MQTT broker settings currently use Serial Monitor or compile-time defaults.

### Competition Event Flow

An allowed RC522 tag read on a slave creates an NFC event for the configured
participant. The slave queues an `EVT` packet with a random transaction ID and
retries it up to five times until it receives a matching `ACK`. The master
deduplicates recent events by sender and transaction ID, then hands the event
to the MQTT publisher. MQTT events are either published to
`<mqttTopic>/validated` or stored in Preferences for later delivery before the
master acknowledges the LoRa event. A slave's short LoRa transmit queue is
RAM-only and is discarded after retries fail.

The master also broadcasts its seconds-since-boot counter every ten seconds.
Slaves report synchronized network uptime for up to 30 seconds after the last
valid sync packet. This is not Unix/UTC time; GPS and NTP are not implemented.
The current protocol uses SX127x radios through the Sandeep Mistry LoRa library;
SX1262 radios such as Heltec WiFi LoRa 32 V3 are not supported by this path.

### Threading

ESPAsyncWebServer runs its handlers in the AsyncTCP FreeRTOS task, not in `loop()`. To avoid races on `BeaconConfig` (which contains heap-allocated `String`s):

- Only the main loop writes `config`, and it holds a mutex while doing so.
- Web handlers read a copy with `beaconConfigSnapshot()`, build the new configuration and hand it over with `beaconQueueConfig()`.
- Test, PTT test, defaults and reboot are queued with `beaconQueueRequest()`; the main loop runs them in `processQueuedRequests()`. Reboot waits 500 ms so the HTTP reply is delivered first.
- Battery values shown on the web page come from the main loop's cached measurement, so the ADC is only accessed from one task. The web controls expose callsign, fox ID, mode, fox sync, startup/TX/idle timing, CW, warble, PTT polarity and guards, battery, Wi-Fi AP timeout, display eco mode, and LoRa settings. Serial additionally configures NFC allowlists and MQTT credentials/settings.

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
| `/api/config` | POST | Validate the submitted fields and queue them for the main loop, which applies and saves them. |
| `/api/test` | POST | Queue one test transmission. |
| `/api/ptt_test` | POST | Queue the PTT-only test. |
| `/api/defaults` | POST | Queue restoring compile-time defaults. |
| `/api/reboot` | POST | Reply, then restart the ESP32 about 500 ms later. |

The AP auto-stops after `wifiApTimeoutMinutes` without a connected station or web activity; `0` disables the timeout. It can be re-enabled from the display menu or Serial Monitor if those controls are available.

## Displays And Power

`DISPLAY_TYPE` selects the display implementation: `0` no display, `1` SSD1306 OLED/U8g2, `2` TFT/TFT_eSPI, or `3` E-Ink/GxEPD2. Board-specific pins and libraries are supplied by the matching PlatformIO environment.

Display updates are throttled; status is refreshed about once per second and the interactive menu at most five times per second. The TFT and E-Ink backends redraw only when the displayed text changes, which avoids TFT flicker and needless E-Ink refreshes. The TFT layout scales with panel height, so the 160x80 Heltec Wireless Tracker uses a small font and still shows every line. Eco mode sleeps the display after four seconds without interaction, with a ten-second boot grace period. OLED uses controller power-save, TFT switches the configured backlight, and E-Ink powers down VEXT and reinitializes the panel with a full refresh after wake. Slow E-Ink refresh is expected and is independent of the beacon state machine.

## Battery Monitoring

Battery measurement is disabled by default. When enabled, firmware samples the selected ADC pin, converts millivolts using `batteryScale`, estimates a single-cell Li-ion percentage and compares voltage with `lowBatteryVoltage`. Measurements (12 averaged samples) are taken at most once per second and cached. A low reading moves the state machine to `LowBatteryHalt` and forces audio/PTT off; the beacon resumes automatically once the voltage is 0.2 V above the cutoff or monitoring is disabled. Install and calibrate the resistor divider before enabling it; never allow an ESP32 ADC input to exceed its permitted voltage.

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

After linking, `post:tools/merge_bin.py` runs `esptool.py merge_bin` with the same offsets, flash mode, frequency and size PlatformIO uses for a USB upload. It writes `.pio/build/<env>/firmware-merged.bin`, a complete image for offset `0x0`. `firmware.bin` alone is only the application (offset `0x10000`) and will not boot on an erased chip.

## CI, Release, And Web Flasher

1. Pushes and pull requests that touch firmware/build inputs run `.github/workflows/build.yml`, which compiles the 32 environments listed in the workflow matrix.
2. To publish firmware, create and push a version tag matching `v*` (or manually run the release workflow with an existing tag). The release workflow builds every environment and attaches two files per board to the GitHub Release: `firmware-<environment>.bin` (merged factory image, offset 0) and `firmware-<environment>-app.bin` (application only, offset 0x10000).
3. When the release workflow finishes (or a release is published manually), the Pages workflow deploys `flasher/` and downloads the firmware assets from the latest GitHub Release into `flasher/firmware/`. The binaries are served from the Pages site because GitHub Release downloads do not send CORS headers.
4. The board selector in `flasher/index.html` builds an ESP Web Tools manifest for the chosen board that points at `firmware/firmware-<environment>.bin`, offset 0. `flasher/manifest.json` lists every board with the same paths.

Without a GitHub Release, the Pages workflow deliberately deploys the flasher without firmware files; the browser page can load but cannot install a build until a Release exists. Keep the PlatformIO environment names, CI/release matrices, manifest build names and selector entries synchronized when adding or removing a board.

## Safe Hardware Bring-Up

1. Build and upload the exact board environment with the radio disconnected.
2. Open Serial Monitor at 115200 baud and run `show`.
3. Run `ptt_test` and confirm the assigned GPIO with a meter or LED.
4. Verify the isolated PTT circuit keys and releases before connecting radio audio.
5. Add audio through an appropriate coupling/attenuation circuit with the level low.
6. Test at minimum practical power or into a suitable dummy load.

A successful PlatformIO build validates source and board configuration, not the wiring or RF behavior of a specific board revision. See [Board And Pin Matrix](board-matrix.md), [Heltec Board Support](heltec-boards.md), [LilyGO Board Support](lilygo-boards.md), and [Wiring Guide](wiring.md).

# Component Reference

This is the reference for every part of the BricoHams Fox Hunt Beacon: the
hardware you build, and every software file in this repository. Use it to find
what a part does, how it connects to the rest, and where to change it.

- [Hardware components](#hardware-components)
- [Firmware components](#firmware-components)
- [Web admin UI](#web-admin-ui)
- [Build tooling](#build-tooling)
- [CI, release and web flasher](#ci-release-and-web-flasher)
- [Documentation files](#documentation-files)

---

## Hardware components

### Block diagram

```text
                   +-------------------------- ESP32 board --------------------------+
 battery / USB --> | 3V3 regulator                                                    |
                   |  PTT GPIO ----> [PTT driver: NPN / MOSFET / optocoupler] --------+--> radio PTT
                   |  AUDIO GPIO --> [RC low-pass] -> [coupling cap] -> [trimpot] ----+--> radio MIC
                   |  BATTERY ADC <- [resistor divider] <----------------------------+--- battery +
                   |  LED GPIO ----> status LED                                       |
                   |  BUTTON GPIO <- push button to GND                               |
                   |  display bus -> OLED / TFT / E-Ink (on display boards)           |
                   +------------------------------------------------------------------+
                                   GND ------------------------------------------------> radio GND
```

GPIO numbers depend on the board environment; see
[Board And Pin Matrix](board-matrix.md) or run `show` over serial.

### Bill of materials

| # | Component | Recommended part / value | Qty | Role |
| --- | --- | --- | ---: | --- |
| 1 | ESP32 board | Any [supported board](board-matrix.md) | 1 | Timing, CW/audio generation, PTT control, configuration, display. |
| 2 | Radio | Handheld FM transceiver with accessory jack (Kenwood-style 2-pin is common), or SA818/DRA818 module | 1 | Generates the RF signal. |
| 3 | Accessory plug/cable | Speaker-mic cable or 2.5/3.5 mm plugs matching the radio | 1 | Carries PTT, mic audio and ground. |
| 4 | PTT switch | NPN transistor (2N3904, BC547) **or** logic-level MOSFET (2N7000) **or** optocoupler (PC817) | 1 | Grounds the radio PTT line when the GPIO is active. |
| 5 | PTT base / LED resistor | 1 kΩ–4.7 kΩ for NPN base; 330–470 Ω for optocoupler LED | 1 | Limits GPIO current. |
| 6 | Audio series resistor | 1 kΩ | 1 | First half of the RC low-pass that smooths the PWM tone. |
| 7 | Audio filter capacitor | 100 nF to GND | 1 | Second half of the RC low-pass (~1.6 kHz corner). |
| 8 | Coupling capacitor | 1 µF | 1 | Blocks DC between the ESP32 and the mic input. |
| 9 | Level trimpot | 10 kΩ | 1 | Sets deviation. Start near minimum. |
| 10 | Status LED | Onboard LED, or LED + 330 Ω | 1 | Blink pattern shows the beacon state. |
| 11 | Push button | Onboard BOOT button, or momentary switch to GND | 1 | Test TX, display wake, on-screen menu. |
| 12 | Battery divider | Two resistors, e.g. 100 kΩ + 100 kΩ (scale 2.0) | 2 | Brings battery voltage into ADC range. Optional. |
| 13 | ADC filter capacitor | 100 nF across the lower divider resistor | 1 | Reduces ADC noise. Optional. |
| 14 | Power source | USB power bank, 1S/2S Li-ion with regulator, or LiFePO4 | 1 | Field power for ESP32 and radio. |
| 15 | Fuse and switch | Inline fuse sized for the radio TX current; toggle switch | 1 each | Field safety and quick shutdown. |
| 16 | Antenna / dummy load | Band-appropriate antenna; 50 Ω dummy load for bench tests | 1 | RF output. |
| 17 | Enclosure | Weather-resistant box with strain relief | 1 | Field deployment. |

### PTT driver (items 4–5)

Most handhelds transmit when the PTT line is pulled to radio ground. The
recommended driver turns on when the ESP32 GPIO goes HIGH, so the firmware
setting is `active_high` (the default).

| Option | Wiring | Notes |
| --- | --- | --- |
| NPN transistor | GPIO → 1–4.7 kΩ → base; emitter → radio GND; collector → radio PTT | Cheapest. Shares ground with the radio. |
| N-MOSFET | GPIO → 100 Ω → gate, 100 kΩ gate → GND; source → radio GND; drain → radio PTT | Use a logic-level part that switches fully at 3.3 V. |
| Optocoupler | GPIO → 330–470 Ω → LED anode, LED cathode → ESP32 GND; transistor side across radio PTT/GND | Galvanic isolation. Best when the radio has its own battery or noisy ground. |

Add a 10–100 kΩ pull-down from the driver input to GND. The ESP32 pin floats
during reset and flashing, and without the pull-down the radio could key.

Use `active_low` only if your circuit keys the radio when the GPIO is LOW. The
firmware applies the saved polarity before it enables the pin, so an
`active_low` interface does not key during boot.

### Audio chain (items 6–9)

```text
AUDIO GPIO -> 1 kΩ -+-> 1 µF -> 10 kΩ trimpot (wiper) -> radio MIC
                    |
                  100 nF
                    |
                   GND
```

The firmware produces a square wave with LEDC PWM (10-bit, 50 % duty) at the
CW tone (default 700 Hz) or the warble tones (700/900 Hz). The RC filter
rounds the edges and the trimpot sets the level. Mic inputs are sensitive, so
start at minimum and raise the level while you listen on a second receiver.

### Battery sense (items 12–13)

`batteryScale = (R_top + R_bottom) / R_bottom`. The ADC uses 11 dB attenuation
(about 3.1 V full scale), so keep the pin voltage below 3.0 V at full charge.

| Battery | Full voltage | Divider (top / bottom) | `battery_scale` | Suggested `low_battery` |
| --- | ---: | --- | ---: | ---: |
| 1S Li-ion | 4.2 V | 100 kΩ / 100 kΩ | 2.0 | 3.4 |
| 2S Li-ion | 8.4 V | 300 kΩ / 100 kΩ | 4.0 | 6.8 |
| 4S LiFePO4 | 14.6 V | 470 kΩ / 100 kΩ | 5.7 | 12.0 |

Calibrate by comparing `show` with a multimeter, and adjust `battery_scale`
until they agree. The percentage shown is for a single Li-ion cell (3.2–4.2 V
after scaling), so for other chemistries rely on the voltage.

### ESP32 board features used

| Feature | Used for |
| --- | --- |
| GPIO output | PTT driver, status LED |
| LEDC PWM | Audio tone |
| ADC | Battery voltage |
| GPIO input + pull-up | Push button |
| NVS flash (Preferences) | Saved settings |
| WiFi (AP mode) | Web admin and captive portal |
| I2C / SPI | OLED, TFT or E-Ink display |

The onboard LoRa radio on Heltec/LilyGO/TTGO boards is **not** used.

---

## Firmware components

```text
            +--------------------+        snapshot / queue        +------------------+
 Serial --> |                    | <----------------------------- |  web_admin.cpp   | <-- browser
 Button --> |     main.cpp       |      (beacon_runtime.h)        |  (AsyncTCP task) |
            |  main loop, state  | -----------------------------> +------------------+
            |  machine, CW, PTT  |   config (read in loop only)
            +---------+----------+
                      | displayUpdate / displayMenu
                      v
               display.cpp  (OLED | TFT | E-Ink | none)
```

### `src/main.cpp`: beacon core

This file runs in the Arduino main loop task and owns all beacon behaviour.

| Area | Key functions | Notes |
| --- | --- | --- |
| Boot | `setup()` | Loads config, sets PTT idle before enabling the pin, starts display, LEDC, ADC, WiFi, then enters `StartupDelay` or calls `finishStartup()`. |
| Main loop | `loop()` | Serial, button, web housekeeping, queued requests, beacon mode switch, low-battery check, test TX, state machine, display. |
| State machine | `enterState()`, `finishStartup()`, `enterIdleAfter()` | States: `StartupDelay`, `Idle`, `Transmitting`, `ContinuousTransmit`, `LowBatteryHalt`. |
| Schedule | `nextTxAt`, `cycleMs()`, `skipMissedSlots()`, `timeReached()` | Absolute, drift-free slot timing. |
| Fox sync | `foxNumberFromId()`, `resolvedStartupDelaySeconds()` | `MOE..MO5` → slot 1..5; startup delay = (slot − 1) × TX seconds. |
| Transmission | `transmitBeacon()`, `runContinuousBeacon()`, `waitWithService()` | PTT lead → CW ID → warble or carrier → tail. Background work runs during the carrier only. |
| CW / audio | `sendMorseText()`, `sendBeaconId()`, `sendWarbleUntil()`, `morseFor()`, `audioTone()`, `audioOff()` | PARIS timing: dot = 1200 / WPM ms. Characters `A–Z 0–9 / -`. |
| PTT | `setPtt()`, `testPttOnly()` | Applies `pttActiveLow`. PTT test keys for 1.2 s. |
| Battery | `sampleBatteryIfDue()`, `readBatteryVoltage()`, `readBatteryPercent()`, `batteryPercentFromVoltage()`, `isLowBattery()` | 12-sample average, at most once per second, cached for other tasks. |
| Persistence | `loadConfig()`, `saveConfig()`, `loadDefaultConfig()`, `restoreDefaults()` | NVS namespace `foxbeacon` (keys below). Values clamped on load. |
| Serial CLI | `readSerialCommands()`, `handleCommand()`, `applySetCommand()`, `handleSetCommand()`, `printConfig()` | 115200 baud, line based, 120-char limit. |
| Button / menu | `onSingleClick()`, `onDoubleClick()`, `onLongPress()`, `menuToggle()`, `renderMenu()` | OneButton, 2 s long press. |
| Display glue | `updateDisplay()`, `stateToString()` | Startup splash (3 s), status (1 Hz), menu (5 Hz), eco sleep (4 s, 10 s boot grace). |
| Cross-task | `beaconConfigSnapshot()`, `beaconQueueConfig()`, `beaconQueueRequest()`, `processQueuedRequests()`, `ConfigLock` | See `beacon_runtime.h`. |
| WiFi control | `applyWifiState()` | Starts or stops the AP to match `wifiApEnabled`. |

Timing constants at the top of the file:

| Constant | Value | Meaning |
| --- | ---: | --- |
| `AUDIO_RESOLUTION_BITS` / `AUDIO_DUTY` | 10 / 512 | 50 % duty square wave |
| `LED_IDLE_BLINK_MS` / `LED_TX_BLINK_MS` / `LED_LOW_BATTERY_BLINK_MS` | 1800 / 160 / 350 | LED patterns |
| `PTT_TEST_MS` | 1200 | PTT-only test length |
| `STARTUP_SCREEN_MS` | 3000 | Splash duration |
| `MENU_TIMEOUT_MS` | 30000 | Menu auto-exit |
| `DISPLAY_ECO_TIMEOUT_MS` / `DISPLAY_ECO_GRACE_MS` | 4000 / 10000 | Eco sleep and boot grace |
| `BATTERY_SAMPLE_INTERVAL_MS` | 1000 | Battery sampling rate |
| `LOW_BATTERY_RECOVERY_HYSTERESIS_V` | 0.20 | Resume threshold above cutoff |
| `REBOOT_DELAY_MS` | 500 | Delay before a web-requested reboot |
| `TX_SERVICE_MARGIN_MS` | 1500 | Stop display/web servicing this close to TX end |

#### Saved settings (NVS namespace `foxbeacon`)

| Key | Field | Type | Range | Default | Serial command |
| --- | --- | --- | --- | --- | --- |
| `call` | `callSign` | string | `A–Z 0–9 / -` | `EA5KAO` | `set call` |
| `fox` | `foxId` | string | `A–Z 0–9 / -` | `MOE` | `set fox` |
| `startup` | `startupDelaySeconds` | u32 | 0–3600 s | 10 | `set startup` |
| `tx` | `transmitSeconds` | u32 | 3–600 s | 60 | `set tx` |
| `idle` | `idleSeconds` | u32 | 5–3600 s | 240 | `set idle` |
| `wpm` | `cwWpm` | u16 | 5–35 | 12 | `set wpm` |
| `tone` | `cwToneHz` | u16 | 300–1800 Hz | 700 | `set tone` |
| `warble` | `warbleEnabled` | bool | | off | `set warble` |
| `warbleLo` | `warbleLowHz` | u16 | 300–1800 Hz | 700 | `set warble_low` |
| `warbleHi` | `warbleHighHz` | u16 | 300–1800 Hz | 900 | `set warble_high` |
| `warbleMs` | `warbleStepMs` | u16 | 100–3000 ms | 350 | `set warble_step` |
| `lead` | `pttLeadMs` | u16 | 50–3000 ms | 350 | `set lead` |
| `tail` | `pttTailMs` | u16 | 50–3000 ms | 350 | `set tail` |
| `pttLow` | `pttActiveLow` | bool | | false (active high) | `set ptt` |
| `batEn` | `batteryEnabled` | bool | | off | `set battery` |
| `batScale` | `batteryScale` | float | 1.0–10.0 | 2.0 | `set battery_scale` |
| `batLow` | `lowBatteryVoltage` | float | 2.5–15.0 V | 3.40 | `set low_battery` |
| `foxSync` | `foxSyncEnabled` | bool | | on | `set fox_sync` |
| `beaconMode` | `beaconMode` | bool | | off (fox) | `set mode` |
| `beaconId` | `beaconIdIntervalSeconds` | u16 | 10–600 s | 60 | `set beacon_id` |
| `wifiAp` | `wifiApEnabled` | bool | | on | `set wifi_ap` |
| `wifiApTo` | `wifiApTimeoutMinutes` | u16 | 0–1440 min | 10 | `set wifi_ap_timeout` |
| `ecoDisp` | `displayEcoMode` | bool | | on | `set eco_mode` |

### `include/beacon_config.h`: defaults and data types

- `FIRMWARE_VERSION`: shown on the splash screen, serial `show` and the web UI.
- `DEFAULT_*` macros: compile-time defaults used for new devices and by
  `defaults`.
- Fallback pins `PTT_PIN`, `AUDIO_PIN`, `LED_PIN`, `BUTTON_PIN` and
  `BATTERY_PIN`, which board environments override with `-D` flags. A
  compile-time `#error` stops the build if any two are the same.
- `struct BeaconConfig`: the runtime settings.
- `enum class BeaconState`: the five beacon states. The web API relies on
  their numeric order.

### `include/beacon_runtime.h`: cross-task interface

This header is the only way code outside the main loop task may touch beacon
state:

| Function | Caller | Effect |
| --- | --- | --- |
| `beaconConfigSnapshot()` | any task | Returns a copy of `config` taken under the mutex. |
| `beaconQueueConfig(next)` | any task | Main loop replaces `config`, saves it and applies WiFi changes. |
| `beaconQueueRequest(r)` | any task | `Test`, `PttTest`, `Defaults` or `Reboot` runs on the main loop. |
| `beaconStateValue()` | any task | Current `BeaconState` as an int. |
| `beaconBatteryVoltage()` / `beaconBatteryPercent()` | any task | Last cached measurement. |
| `normalizeId()`, `foxNumberFromId()`, `resolvedStartupDelaySeconds(cfg)` | any task | Pure helpers. |

### `src/web_admin.cpp` + `include/web_admin.h`: WiFi AP and HTTP API

| Function | Purpose |
| --- | --- |
| `webAdminInit(prefix)` | Starts the open AP `<prefix>-XXXX` (last two MAC bytes) at 10.0.0.8, a captive DNS server and the HTTP server. Registers routes once. Does nothing if already running. |
| `webAdminLoop()` | Main loop only. Serves DNS, counts connected stations as activity, applies the auto-off timeout (not saved to flash, so the AP returns on reboot). |
| `webAdminStop()` | Stops HTTP, DNS and WiFi (radio off). |
| `webAdminIsRunning()`, `webAdminGetIp()`, `webAdminGetSsid()` | Status for display and serial. |

HTTP handlers run in the AsyncTCP task. They use only `beacon_runtime.h`.

| Route | Method | Handler | Response |
| --- | --- | --- | --- |
| `/` | GET | `handleRoot` | gzip HTML, `no-store` |
| `/style.css`, `/script.js` | GET | `handleStyle`, `handleScript` | gzip, cached 1 h |
| `/api/config` | GET | `handleGetConfig` | JSON (below) |
| `/api/config` | POST | `handlePostConfig` | Form fields named like the serial keys (`call`, `fox`, `mode`, `fox_sync`, `startup`, `tx`, `idle`, `beacon_id`, `wpm`, `tone`, `warble`, `warble_low`, `warble_high`, `warble_step`, `ptt`, `lead`, `tail`, `battery`, `battery_scale`, `low_battery`, `wifi_ap`, `wifi_ap_timeout`, `eco_mode`). `call` is required; missing fields keep their value. |
| `/api/test` | POST | `handleTest` | queues a test TX |
| `/api/ptt_test` | POST | `handlePttTest` | queues a PTT test |
| `/api/defaults` | POST | `handleDefaults` | queues a defaults restore |
| `/api/reboot` | POST | `handleReboot` | replies, then reboots after 500 ms |
| anything else | any | `handleNotFound` | redirect to `http://10.0.0.8/` (captive portal) |

`GET /api/config` JSON fields: `call`, `fox`, `beaconMode`, `foxSync`,
`foxNum`, `startup`, `startupResolved`, `tx`, `idle`, `beaconId`, `wpm`,
`tone`, `warble`, `warbleLow`, `warbleHigh`, `warbleStep`, `pttActiveLow`,
`lead`, `tail`, `batteryEnabled`, `batteryScale`, `lowBattery`, `wifiAp`,
`wifiApTimeout`, `ecoMode`, `state`, `battery`, `batteryPct` (when the monitor
is enabled), `apSsid`, `ip`, `version`. Strings are JSON-escaped.

### `src/display.cpp` + `include/display.h`: display abstraction

One API, four compile-time backends selected by `DISPLAY_TYPE`:

| `DISPLAY_TYPE` | Backend | Library | Boards |
| ---: | --- | --- | --- |
| 0 | No-op stubs | none | DevKits, T1, T7, T3-S3, T-OI Plus, Capsule Sensor |
| 1 | SSD1306 OLED 128x64, I2C | U8g2 | Heltec WiFi Kit/LoRa/Stick, TTGO LoRa32, T-Beam |
| 2 | ST7789 / ST7735 TFT, SPI | TFT_eSPI | T-Display, T-Display S3, T-Watch, Wireless Tracker, Vision Master T190 |
| 3 | SSD1680 E-Ink, SPI | GxEPD2 | Wireless Paper, Vision Master E213/E290 |

| Function | Purpose |
| --- | --- |
| `displayInit(call, version)` | Power rails (VEXT/backlight), bus, controller init, splash. |
| `displayStartupScreen(call, version)` | "`CALL` Fox", version, "Starting...". |
| `displayUpdate(call, fox, mode, state, timing, battery, ip)` | Status screen with a state-coded accent bar. |
| `displayMenu(selected, count, labels, values)` | Settings menu, scrolled to keep the selection visible. |
| `displayAPMode(ssid, ip)` | AP info screen (available, currently unused). |
| `displayPower(on)` | Eco sleep and wake: OLED power-save, TFT backlight, E-Ink VEXT plus re-init. |
| `displayClear()` | Blank the screen. |

Backend details:

- **OLED:** full frame buffer, redrawn each update. It is cheap over I2C.
- **TFT:** redraws only when the text changes (no flicker). The layout
  scales with panel height, so panels under 120 px use font 1 and 10 px rows.
  It checks at compile time that beacon GPIOs do not overlap TFT pins.
- **E-Ink:** redraws only when the text changes. The first refresh after init
  is full, later ones are partial. It has the same compile-time pin-conflict
  check as TFT.

### `include/display_config.h`

Contains the fallback `DISPLAY_TYPE` (0), the OLED I2C pins, size and address,
and the TFT backlight/VEXT polarity macros. Board environments override all of
these.

### `include/display_colors.h`

RGB565 palette (`FOX_*`) and semantic roles (`COLOR_*`). TFT uses the real
colours; OLED and E-Ink map them to black/white, inverted text and bar
patterns.

| State | TFT colour | OLED / E-Ink accent bar |
| --- | --- | --- |
| TX / BEACON | green | solid |
| LOWBAT | red | double dash |
| STARTUP | orange | dotted |
| IDLE | grey (text cyan) | dashed |

### Libraries

| Library | Version | Used by |
| --- | --- | --- |
| ESPAsyncWebServer | ^3.6.0 | web admin HTTP |
| AsyncTCP | ^3.3.2 | ESPAsyncWebServer transport |
| OneButton | 2.6.2 | button click, double-click, long press |
| U8g2 | ^2.28.8 | OLED backend |
| TFT_eSPI | ^2.5.0 | TFT backend |
| GxEPD2 | ^1.5.6 | E-Ink backend |
| Preferences, WiFi, DNSServer | Arduino-ESP32 core | settings, AP, captive DNS |

---

## Web admin UI

Source in `data_embed/`. It is embedded in firmware and served gzip-compressed.

| File | Contents |
| --- | --- |
| `index.html` | One form grouped into Identification, Timing, CW/Audio, PTT, Battery, Display & WiFi, plus Save / Test TX / PTT Test / Defaults / Reboot buttons. |
| `script.js` | `loadConfig()` fills the form from `GET /api/config` and renders the status line (state, battery, fox slot, AP, IP, firmware version). `save()` posts the whole form. `cmd(name)` posts to `/api/<name>`. |
| `style.css` | Dark, phone-first layout (max 600 px). |

To change the UI, edit these files and rebuild. `tools/compress.py`
regenerates `include/web_assets.h`.

---

## Build tooling

| File | Stage | What it does |
| --- | --- | --- |
| `platformio.ini` | config | `[env]` shared settings (espressif32 ^6.7, Arduino, 115200 monitor, libraries, extra scripts). Shared display sections (`[oled_*]`, `[tft_*]`, `[eink_*]`), per-vendor GPIO profiles (`[heltec_*_common]`, `[lilygo_*_common]`) and 32 `[env:<board>]` sections with pin `-D` flags. Default env `esp32dev`. |
| `tools/compress.py` | pre-build | gzip level 9 with `mtime=0` of the three `data_embed/` files into `include/web_assets.h` (`INDEX_HTML`, `STYLE_CSS`, `SCRIPT_JS` + `_LEN`). Rewrites the header only when the bytes change. |
| `tools/merge_bin.py` | post-build | `esptool.py merge_bin` of bootloader, partition table, boot_app0 and app into `.pio/build/<env>/firmware-merged.bin` for offset 0, using the board's flash mode, frequency and size. |
| `include/web_assets.h` | generated | Do not edit; ignored by git. |
| `.vscode/` | editor | PlatformIO IDE recommendations and IntelliSense config. |

---

## CI, release and web flasher

| File | Trigger | What it does |
| --- | --- | --- |
| `.github/workflows/build.yml` | push/PR touching `src/`, `include/`, `data_embed/`, `tools/`, `platformio.ini` | Builds all 32 environments; uploads `firmware.bin`, `firmware-merged.bin`, `firmware.elf` as artifacts (7 days). |
| `.github/workflows/release.yml` | tag `v*` or manual with a tag | Builds all 32 environments; publishes `firmware-<env>.bin` (merged, offset 0) and `firmware-<env>-app.bin` (app, offset 0x10000) to a GitHub Release. |
| `.github/workflows/pages.yml` | changes in `flasher/`, release workflow completion, release published, manual | Copies the latest Release's `firmware-*` assets into `flasher/firmware/` and deploys `flasher/` to GitHub Pages. |
| `flasher/index.html` | browser | Board picker (32 boards, filter by display type). Builds a one-board ESP Web Tools manifest pointing at `firmware/firmware-<env>.bin`, offset 0, and offers an erase on first install. |
| `flasher/manifest.json` | browser | Static manifest listing every board with the same relative paths. |
| `flasher/CNAME`, `robots.txt`, `sitemap.xml`, `.nojekyll` | Pages | Custom domain `fox.hamradio.my` and crawler settings. |
| `flasher/wiring-diagram.svg`, `bricohams_horizontal.png` | Pages | Images used by the flasher page. |

When you add or remove a board, update all of these together:

1. an `[env:<name>]` in `platformio.ini`
2. the matrix in `build.yml` and `release.yml`
3. the `BOARDS` list in `flasher/index.html`
4. `flasher/manifest.json`
5. [board-matrix.md](board-matrix.md) and the README board table

---

## Documentation files

| File | Audience | Contents |
| --- | --- | --- |
| `README.md` | everyone | Overview, quick start, build, docs index |
| `docs/understanding.md` | new users | ARDF, IARU timing, signal format |
| `docs/installation.md` | builders | PlatformIO/VS Code build and upload |
| `docs/configuration.md` | operators | All settings and serial commands |
| `docs/wiring.md` | builders | Radio PTT/audio interface |
| `docs/components.md` | builders, contributors | This file |
| `docs/board-matrix.md` | builders | Environments, GPIO profiles, display buses |
| `docs/heltec-boards.md`, `docs/lilygo-boards.md` | builders | Board-specific notes |
| `docs/field-checklist.md` | operators | Pre-hunt checks |
| `docs/troubleshooting.md` | everyone | Symptoms and fixes |
| `docs/software-architecture.md` | contributors | Boot, state machine, threading, CI/release flow |
| `ACKNOWLEDGMENTS.md`, `LICENSE` | everyone | 9M2PJU credit, GPL-3.0-or-later |

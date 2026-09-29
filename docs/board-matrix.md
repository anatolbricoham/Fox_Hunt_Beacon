# Board And Pin Matrix

This is the complete PlatformIO board list for the BricoHams Fox Hunt Beacon. Choose the environment that matches the exact board and revision; a successful build does not prove that a GPIO is exposed or electrically safe on a particular revision.

The firmware controls an external FM radio or transmitter through a PTT interface and audio input. It does not use the onboard LoRa radios on Heltec, LilyGO, or TTGO boards as an ARDF FM transmitter. Use an isolated transistor, MOSFET, or optocoupler interface for PTT; do not connect an unknown radio line directly to an ESP32 GPIO.

## Environments

| PlatformIO environment | Board | Chip | Display | GPIO profile |
| --- | --- | --- | --- | --- |
| `esp32dev` | Generic ESP32 Dev Module | ESP32 | None | Classic ESP32 |
| `esp32doit-devkit-v1` | DOIT ESP32 DevKit V1 | ESP32 | None | Classic ESP32 |
| `lolin32` | WEMOS LOLIN32 | ESP32 | None | Classic ESP32 |
| `esp32-s3-devkitc-1` | Generic ESP32-S3 DevKitC-1 | ESP32-S3 | None | Generic S3 |
| `esp32-c3-devkitm-1` | Generic ESP32-C3 DevKitM-1 | ESP32-C3 | None | Generic C3 |
| `heltec-wifi-kit-32` | Heltec WiFi Kit 32 | ESP32 | SSD1306 OLED | Heltec classic |
| `heltec-wifi-kit-32-v2` | Heltec WiFi Kit 32 V2 | ESP32 | SSD1306 OLED | Heltec classic |
| `heltec-wifi-kit-32-v3` | Heltec WiFi Kit 32 V3 | ESP32-S3 | SSD1306 OLED | Heltec S3 OLED |
| `heltec-wifi-lora-32` | Heltec WiFi LoRa 32 | ESP32 | SSD1306 OLED | Heltec classic |
| `heltec-wifi-lora-32-v2` | Heltec WiFi LoRa 32 V2 | ESP32 | SSD1306 OLED | Heltec classic |
| `heltec-wifi-lora-32-v3` | Heltec WiFi LoRa 32 V3 | ESP32-S3 | SSD1306 OLED | Heltec S3 OLED |
| `heltec-wireless-stick` | Heltec Wireless Stick | ESP32 | SSD1306 OLED | Heltec classic |
| `heltec-wireless-stick-lite` | Heltec Wireless Stick Lite | ESP32 | SSD1306 OLED | Heltec classic |
| `heltec-wireless-stick-lite-v3` | Heltec Wireless Stick Lite V3 | ESP32-S3 | SSD1306 OLED | Heltec S3 OLED, compatibility target |
| `heltec-wireless-tracker` | Heltec Wireless Tracker | ESP32-S3 | ST7735 TFT, 160x80 | Heltec Tracker |
| `heltec-vision-master-t190` | Heltec Vision Master T190 | ESP32-S3 | ST7789 TFT, 170x320 | Vision T190 |
| `heltec-wireless-paper` | Heltec Wireless Paper | ESP32-S3 | SSD1680 E-Ink, 250x122 | Wireless Paper |
| `heltec-vision-master-e213` | Heltec Vision Master E213 | ESP32-S3 | SSD1680 E-Ink, 250x122 | Vision E-Ink |
| `heltec-vision-master-e290` | Heltec Vision Master E290 | ESP32-S3 | SSD1680 E-Ink, 296x128 | Vision E-Ink |
| `heltec-capsule-sensor-v3` | Heltec Capsule Sensor V3 | ESP32-S3 | None | Heltec Vision no-display |
| `ttgo-lora32-v1` | TTGO LoRa32 V1 | ESP32 | SSD1306 OLED | LilyGO LoRa classic |
| `ttgo-lora32-v2` | TTGO LoRa32 V2 | ESP32 | SSD1306 OLED | LilyGO LoRa classic |
| `ttgo-lora32-v21` | TTGO LoRa32 V2.1.6 | ESP32 | SSD1306 OLED | LilyGO LoRa classic |
| `ttgo-t-beam` | TTGO T-Beam | ESP32 | SSD1306 OLED | T-Beam |
| `lilygo-t-display` | LilyGO T-Display | ESP32 | ST7789 TFT, 135x240 | LilyGO classic |
| `lilygo-t-display-s3` | LilyGO T-Display S3 | ESP32-S3 | ST7789 TFT, 170x320 | LilyGO S3 |
| `ttgo-t-watch` | TTGO T-Watch | ESP32 | ST7789 TFT, 240x240 | LilyGO classic |
| `lilygo-t3-s3` | LilyGO T3-S3 | ESP32-S3 | None | LilyGO S3 |
| `ttgo-t-oi-plus` | TTGO T-OI Plus | ESP32-C3 | None | LilyGO C3 |
| `ttgo-t1` | TTGO T1 | ESP32 | None | LilyGO classic |
| `ttgo-t7-v13-mini32` | TTGO T7 V1.3 Mini32 | ESP32 | None | LilyGO classic |
| `ttgo-t7-v14-mini32` | TTGO T7 V1.4 Mini32 | ESP32 | None | LilyGO classic |

## PTT, Audio, LED, Button, And Battery GPIO

These are the build-time assignments in `platformio.ini` and `include/beacon_config.h`. The battery ADC is disabled by default; its pin is only read after battery monitoring is enabled and a correctly rated divider is installed.

| GPIO profile | PTT | Audio | Status LED | Button | Battery ADC | Environments |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Classic ESP32 | 25 | 26 | 2 | 0 | 34 | `esp32dev`, `esp32doit-devkit-v1`, `lolin32` |
| Generic S3 | 6 | 17 | 2 | 0 | 4 | `esp32-s3-devkitc-1` |
| Generic C3 | 7 | 5 | 8 | 9 | 3 | `esp32-c3-devkitm-1`, `ttgo-t-oi-plus` |
| Heltec classic | 13 | 17 | 25 | 0 | 36 | Heltec classic ESP32 OLED boards |
| Heltec S3 OLED | 6 | 7 | 35 | 0 | 1 | Heltec WiFi Kit/LoRa V3, Wireless Stick Lite V3 |
| Heltec Tracker | 7 | 6 | 18 | 0 | 1 | `heltec-wireless-tracker` |
| Vision T190 | 15 | 16 | 18 | 0 | 6 | `heltec-vision-master-t190` |
| Wireless Paper | 15 | 16 | 18 | 0 | 1 | `heltec-wireless-paper` |
| Vision E-Ink | 15 | 16 | 45 | 0 | 7 | `heltec-vision-master-e213`, `heltec-vision-master-e290` |
| Heltec Vision no-display | 4 | 3 | 18 | 0 | 6 | `heltec-capsule-sensor-v3` |
| LilyGO classic | 25 | 26 | 2 | 0 | 34 | LilyGO T-Display, T-Watch, T1, T7 V1.3/V1.4 |
| LilyGO LoRa classic | 25 | 13 | 2 | 0 | 35 | TTGO LoRa32 V1/V2/V2.1.6 |
| T-Beam | 25 | 13 | 4 | 0 | 35 | `ttgo-t-beam` |
| LilyGO S3 | 6 | 7 | 38 | 0 | 4 | LilyGO T-Display S3, T3-S3 |

The build has compile-time checks for duplicate beacon GPIOs and conflicts between beacon GPIOs and configured TFT/E-Ink pins. A profile can still compile while being unsuitable for a different hardware revision, so verify the board silkscreen and schematic before wiring.

## Display Bus GPIO

### OLED

| OLED profile | SDA | SCL | Reset | Used by |
| --- | ---: | ---: | ---: | --- |
| Classic OLED | 4 | 15 | 16 | Heltec classic boards, TTGO LoRa32 V1 |
| Heltec S3 OLED | 17 | 18 | 21 | Heltec WiFi Kit/LoRa V3, Wireless Stick Lite V3 |
| TTGO LoRa32 V2 | 21 | 22 | 16 | `ttgo-lora32-v2` |
| TTGO LoRa32 V2.1.6 | 8 | 9 | 16 | `ttgo-lora32-v21` |
| T-Beam | 21 | 22 | 16 | `ttgo-t-beam` |

The OLED driver is U8g2 SSD1306 over I2C.

### TFT

| TFT profile | Controller/size | MOSI | SCLK | CS | DC | Reset | Backlight | Power |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| LilyGO T-Display | ST7789, 240x135 landscape | 19 | 18 | 5 | 16 | 23 | 4 | None configured |
| LilyGO T-Display S3 | ST7789, 320x170 landscape | 13 | 14 | 10 | 11 | 12 | 2 | None configured |
| TTGO T-Watch | ST7789, 240x240 | 19 | 18 | 5 | 27 | 33 | 12 | None configured |
| Heltec Tracker | ST7735, 160x80 | 42 | 41 | 38 | 40 | 39 | 21 | VEXT GPIO 3, active HIGH |
| Vision Master T190 | ST7789, 320x170 landscape | 48 | 38 | 39 | 47 | 40 | 17 | GPIO 5, 17, 46 HIGH; GPIO 7 LOW |

### E-Ink

| E-Ink profile | Panel class | SCLK | MOSI | CS | DC | Reset | Busy | Power |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Wireless Paper | SSD1680, 250x122 | 3 | 2 | 4 | 5 | 6 | 7 | VEXT GPIO 45, active LOW |
| Vision Master E213 | SSD1680, 250x122 | 4 | 6 | 5 | 2 | 3 | 1 | GPIO 18 and 46 HIGH |
| Vision Master E290 | SSD1680, 296x128 | 2 | 1 | 3 | 4 | 5 | 6 | GPIO 18 and 46 HIGH |

E-Ink power-down requires a full controller reinitialization and full refresh on wake. E-Ink updates are slower than OLED/TFT; avoid interpreting a slow refresh as a frozen ESP32.

## Build, Upload, And Monitor

The default environment is `esp32dev`:

```sh
pio run
pio run -e heltec-wifi-lora-32-v3
pio run -e heltec-wireless-paper
pio run -e lilygo-t-display-s3
```

Upload only after selecting the exact environment:

```sh
pio run -e heltec-wifi-lora-32-v3 -t upload
pio device monitor -b 115200
```

See [Heltec Board Support](heltec-boards.md), [LilyGO Board Support](lilygo-boards.md), and [Wiring Guide](wiring.md) for model-specific guidance and safe radio-interface testing.

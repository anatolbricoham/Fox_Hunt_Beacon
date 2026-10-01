#pragma once

#include <Arduino.h>

// Firmware version shown on startup screen and web admin UI.
#define FIRMWARE_VERSION "1.1.0"

// Main user configuration file.
//
// Edit these values before building if you want fixed defaults in the firmware.
// After flashing, Serial Monitor commands can override these values and save
// them to ESP32 flash. Use the "defaults" serial command to restore these
// compile-time defaults.

// What the beacon sends at the start of every transmission.
#define DEFAULT_CALLSIGN "EA5KAO"
#define DEFAULT_FOX_ID "MOE"

// Main timing. Defaults follow the IARU Region 1 ARDF 5-fox cycle: each fox
// transmits for 60 seconds, then waits 240 seconds while the other four foxes

// take their turns. The full cycle is 300 seconds (5 minutes).
#define DEFAULT_STARTUP_DELAY_SECONDS 10
#define DEFAULT_TRANSMIT_SECONDS 60
#define DEFAULT_IDLE_SECONDS 240

// Fox-slot synchronization. When enabled, the firmware derives the fox number
// from the fox ID (MOE=1, MOI=2, MOS=3, MOH=4, MO5=5) and overrides the startup
// delay with (foxNumber - 1) * transmitSeconds so that five beacons started at
// roughly the same time fall into the standard round-robin order.
#define DEFAULT_FOX_SYNC_ENABLED 1

// Continuous beacon mode (finish-line / MO6 beacon). When enabled, the beacon
// keys PTT continuously and re-sends the CW ID every beaconIdIntervalSeconds.
// This is not part of the 5-fox round-robin; it runs on a separate frequency.
#define DEFAULT_BEACON_MODE 0
#define DEFAULT_BEACON_ID_INTERVAL_SECONDS 60

// Morse/CW ID.
#define DEFAULT_CW_WPM 12
#define DEFAULT_CW_TONE_HZ 700

// Optional attention tone after the CW ID. Disabled by default for IARU
// standard compliance: competition foxes send a CW ID followed by a steady
// carrier, not a warble. Enable for training or attention-tone use.
#define DEFAULT_WARBLE_ENABLED 0
#define DEFAULT_WARBLE_LOW_HZ 700
#define DEFAULT_WARBLE_HIGH_HZ 900
#define DEFAULT_WARBLE_STEP_MS 350

// Radio keying. This is the ESP32 GPIO active level, not the radio PTT line
// polarity. For the recommended NPN/MOSFET/opto interface, a HIGH GPIO usually
// turns the interface on and pulls the radio PTT line to ground.
#define DEFAULT_PTT_ACTIVE_LOW 0
#define DEFAULT_PTT_LEAD_MS 350
#define DEFAULT_PTT_TAIL_MS 350

// Battery monitor. Leave disabled until the resistor divider is built and
// calibrated.
#define DEFAULT_BATTERY_ENABLED 0
#define DEFAULT_BATTERY_SCALE 2.0f
#define DEFAULT_LOW_BATTERY_VOLTAGE 3.40f

// Display eco mode. When enabled, the display sleeps after a short period of
// inactivity to save power — desirable for a battery-powered fox hunt beacon.
// Wakes on any button press.
#define DEFAULT_DISPLAY_ECO_MODE 1

// NFC and MQTT competition mode. These are disabled by default and only take
// effect when the optional hardware is installed and enabled in the config.
#define DEFAULT_NFC_ENABLED 0
#define DEFAULT_NFC_TAG_WHITELIST ""
#define DEFAULT_MQTT_ENABLED 0
#define DEFAULT_MQTT_BROKER "broker.local"
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_MQTT_TOPIC "foxhunt/competition"
#define DEFAULT_WIFI_STATION_SSID ""
#define DEFAULT_WIFI_STATION_PASSWORD ""
#define DEFAULT_MQTT_USER ""
#define DEFAULT_MQTT_PASSWORD ""
#define DEFAULT_PARTICIPANT_NAME "competitor"

// LoRa network for master/slave uptime sync and competition event relay.
#define DEFAULT_LORA_ENABLED 0
#define DEFAULT_LORA_MASTER_MODE 0
#define DEFAULT_LORA_FREQUENCY_HZ 868100000UL
#define DEFAULT_LORA_TX_POWER_DBM 20
#define DEFAULT_LORA_SYNC_WORD 0x12
#define DEFAULT_LORA_NODE_ID "node-01"

// Default SPI pins for the optional RFID/NFC reader. Boards with displays may
// override these values in platformio.ini to avoid pin conflicts.
#ifndef NFC_SDA_PIN
#define NFC_SDA_PIN 5
#endif
#ifndef NFC_RST_PIN
#define NFC_RST_PIN 22
#endif
#ifndef NFC_SCK_PIN
#define NFC_SCK_PIN 18
#endif
#ifndef NFC_MISO_PIN
#define NFC_MISO_PIN 19
#endif
#ifndef NFC_MOSI_PIN
#define NFC_MOSI_PIN 23
#endif

#ifndef LORA_CS_PIN
#define LORA_CS_PIN 16
#endif
#ifndef LORA_RST_PIN
#define LORA_RST_PIN 17
#endif
#ifndef LORA_DIO0_PIN
#define LORA_DIO0_PIN 21
#endif
#ifndef LORA_SCK_PIN
#define LORA_SCK_PIN NFC_SCK_PIN
#endif
#ifndef LORA_MISO_PIN
#define LORA_MISO_PIN NFC_MISO_PIN
#endif
#ifndef LORA_MOSI_PIN
#define LORA_MOSI_PIN NFC_MOSI_PIN
#endif

// Default pins for classic ESP32 DevKit-style boards. Some board environments
// override these in platformio.ini.
#ifndef PTT_PIN
#define PTT_PIN 25
#endif

#ifndef AUDIO_PIN
#define AUDIO_PIN 26
#endif

#ifndef LED_PIN
#define LED_PIN 2
#endif

#ifndef BUTTON_PIN
#define BUTTON_PIN 0
#endif

#ifndef BATTERY_PIN
#define BATTERY_PIN 34
#endif

#if PTT_PIN == AUDIO_PIN || PTT_PIN == LED_PIN || PTT_PIN == BUTTON_PIN || PTT_PIN == BATTERY_PIN || \
  AUDIO_PIN == LED_PIN || AUDIO_PIN == BUTTON_PIN || AUDIO_PIN == BATTERY_PIN || \
  LED_PIN == BUTTON_PIN || LED_PIN == BATTERY_PIN || BUTTON_PIN == BATTERY_PIN
#error "Beacon GPIO assignments must be unique"
#endif

// Runtime configuration structure. Stored in ESP32 flash via Preferences.
// Defaults come from the #defines above.
struct BeaconConfig {
  String callSign = DEFAULT_CALLSIGN;
  String foxId = DEFAULT_FOX_ID;
  uint32_t startupDelaySeconds = DEFAULT_STARTUP_DELAY_SECONDS;
  uint32_t transmitSeconds = DEFAULT_TRANSMIT_SECONDS;
  uint32_t idleSeconds = DEFAULT_IDLE_SECONDS;
  uint16_t cwWpm = DEFAULT_CW_WPM;
  uint16_t cwToneHz = DEFAULT_CW_TONE_HZ;
  uint16_t warbleLowHz = DEFAULT_WARBLE_LOW_HZ;
  uint16_t warbleHighHz = DEFAULT_WARBLE_HIGH_HZ;
  uint16_t warbleStepMs = DEFAULT_WARBLE_STEP_MS;
  uint16_t pttLeadMs = DEFAULT_PTT_LEAD_MS;
  uint16_t pttTailMs = DEFAULT_PTT_TAIL_MS;
  bool pttActiveLow = DEFAULT_PTT_ACTIVE_LOW;
  bool warbleEnabled = DEFAULT_WARBLE_ENABLED;
  bool batteryEnabled = DEFAULT_BATTERY_ENABLED;
  float batteryScale = DEFAULT_BATTERY_SCALE;
  float lowBatteryVoltage = DEFAULT_LOW_BATTERY_VOLTAGE;
  bool foxSyncEnabled = DEFAULT_FOX_SYNC_ENABLED;
  bool beaconMode = DEFAULT_BEACON_MODE;
  uint16_t beaconIdIntervalSeconds = DEFAULT_BEACON_ID_INTERVAL_SECONDS;
  bool wifiApEnabled = true;       // WiFi AP + web admin UI on/off
  uint16_t wifiApTimeoutMinutes = 10; // Auto-off AP after N minutes of no activity (0 = never)
  bool displayEcoMode = DEFAULT_DISPLAY_ECO_MODE;     // Turn off display after inactivity

  // Competition mode for NFC validation and MQTT reporting.
  bool nfcEnabled = DEFAULT_NFC_ENABLED;
  String nfcAllowedTags = DEFAULT_NFC_TAG_WHITELIST;
  String participantName = DEFAULT_PARTICIPANT_NAME;
  bool mqttEnabled = DEFAULT_MQTT_ENABLED;
  String wifiStationSsid = DEFAULT_WIFI_STATION_SSID;
  String wifiStationPassword = DEFAULT_WIFI_STATION_PASSWORD;
  String mqttBroker = DEFAULT_MQTT_BROKER;
  uint16_t mqttPort = DEFAULT_MQTT_PORT;
  String mqttTopic = DEFAULT_MQTT_TOPIC;
  String mqttUser = DEFAULT_MQTT_USER;
  String mqttPassword = DEFAULT_MQTT_PASSWORD;

  bool loraEnabled = DEFAULT_LORA_ENABLED;
  bool loraMasterMode = DEFAULT_LORA_MASTER_MODE;
  String loraNodeId = DEFAULT_LORA_NODE_ID;
  uint32_t loraFrequencyHz = DEFAULT_LORA_FREQUENCY_HZ;
  int8_t loraTxPowerDbm = DEFAULT_LORA_TX_POWER_DBM;
  uint8_t loraSyncWord = DEFAULT_LORA_SYNC_WORD;
};

// Beacon state machine states.
enum class BeaconState {
  StartupDelay,
  Idle,
  Transmitting,
  ContinuousTransmit,
  LowBatteryHalt,
};

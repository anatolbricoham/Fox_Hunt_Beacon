#include <Arduino.h>
#include <Preferences.h>
#include <OneButton.h>
#include <atomic>
#include "beacon_config.h"
#include "beacon_runtime.h"
#include "display.h"
#include "web_admin.h"

constexpr uint8_t AUDIO_CHANNEL = 0;
constexpr uint8_t AUDIO_RESOLUTION_BITS = 10;
constexpr uint16_t AUDIO_DUTY = 512;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t LED_IDLE_BLINK_MS = 1800;
constexpr uint32_t LED_TX_BLINK_MS = 160;
constexpr uint32_t LED_LOW_BATTERY_BLINK_MS = 350;
constexpr uint32_t PTT_TEST_MS = 1200;
constexpr uint32_t STARTUP_SCREEN_MS = 3000;
constexpr uint32_t MENU_TIMEOUT_MS = 30000;
constexpr uint32_t MENU_UPDATE_INTERVAL_MS = 200;
constexpr uint32_t DISPLAY_ECO_TIMEOUT_MS = 4000;
constexpr uint32_t DISPLAY_ECO_GRACE_MS = 10000;  // no eco sleep for first 10s after boot
constexpr uint32_t BATTERY_SAMPLE_INTERVAL_MS = 1000;
constexpr float LOW_BATTERY_RECOVERY_HYSTERESIS_V = 0.20f;
constexpr uint32_t REBOOT_DELAY_MS = 500;       // let the HTTP reply leave first
constexpr uint32_t TX_SERVICE_MARGIN_MS = 1500;  // stop slow display work near TX end
const char *const AP_NAME_PREFIX = "BricoHams-Fox";

Preferences preferences;
BeaconConfig config;
volatile BeaconState state = BeaconState::StartupDelay;
uint32_t stateStartedAt = 0;
uint32_t lastLedToggleAt = 0;
bool ledOn = false;
bool forceTransmit = false;
String serialLine;
uint32_t lastDisplayUpdateAt = 0;

// Absolute schedule: millis() at which the next scheduled transmission starts.
// Advancing it by exactly one cycle keeps the IARU round-robin slot from
// drifting and lets a test transmission run without moving the slot.
uint32_t nextTxAt = 0;

// Battery: sampled at most once per BATTERY_SAMPLE_INTERVAL_MS by the main
// loop. Other tasks read the cached values only.
volatile float batteryVoltageCache = 0.0f;
volatile uint8_t batteryPercentCache = 0;
uint32_t lastBatterySampleAt = 0;
bool batteryCacheValid = false;

// Cross-task request queue (see beacon_runtime.h).
SemaphoreHandle_t configMutex = nullptr;
BeaconConfig pendingConfig;
bool pendingConfigValid = false;
std::atomic<uint8_t> pendingRequests{0};
bool rebootPending = false;
uint32_t rebootRequestedAt = 0;

// Holds the config mutex for the lifetime of the object. The main loop takes
// it while it writes config; other tasks take it while they copy config.
class ConfigLock {
 public:
  ConfigLock() {
    if (configMutex) xSemaphoreTake(configMutex, portMAX_DELAY);
  }
  ~ConfigLock() {
    if (configMutex) xSemaphoreGive(configMutex);
  }
};

// Menu state machine
enum class DisplayMode {
  StartupScreen,
  Status,
  Menu,
};
DisplayMode displayMode = DisplayMode::StartupScreen;
uint32_t displayModeEnteredAt = 0;
int menuIndex = 0;
uint32_t lastActivityAt = 0;
bool displaySleeping = false;

// OneButton instance for debounced multi-click detection.
#ifdef BUTTON_PIN
OneButton button(BUTTON_PIN, true);  // active LOW with internal pull-up
#endif

// Menu items: on/off toggles that can be changed from the display menu.
// Full settings (callsign, fox ID, timing, CW, PTT, etc.) require the web UI.
enum MenuItem {
  MENU_WIFI_AP = 0,
  MENU_WARBLE,
  MENU_FOX_SYNC,
  MENU_BATTERY,
  MENU_BEACON_MODE,
  MENU_ECO_MODE,
  MENU_EXIT,
  MENU_COUNT
};
const char *const menuLabels[] = {
  "WiFi AP",
  "Warble",
  "Fox Sync",
  "Battery",
  "Mode",
  "Eco Disp",
  "Exit",
};

float readBatteryVoltage();
uint8_t readBatteryPercent();
void saveConfig();
void loadDefaultConfig();
void enterState(BeaconState nextState);
uint32_t elapsedSince(uint32_t startedAt);
void enterIdleAfter(uint32_t delayMs);
void applyWifiState();

// --- Cross-task API (beacon_runtime.h) ---

int beaconStateValue() {
  return static_cast<int>(state);
}

BeaconConfig beaconConfigSnapshot() {
  ConfigLock lock;
  return config;
}

void beaconQueueConfig(const BeaconConfig &next) {
  ConfigLock lock;
  pendingConfig = next;
  pendingConfigValid = true;
}

void beaconQueueRequest(BeaconRequest request) {
  pendingRequests.fetch_or(static_cast<uint8_t>(request));
}

float beaconBatteryVoltage() {
  return batteryVoltageCache;
}

uint8_t beaconBatteryPercent() {
  return batteryPercentCache;
}

// Get the current value string for a menu item.
const char *menuValueStr(int idx) {
  switch (idx) {
    case MENU_WIFI_AP:    return config.wifiApEnabled ? "ON" : "OFF";
    case MENU_WARBLE:     return config.warbleEnabled ? "ON" : "OFF";
    case MENU_FOX_SYNC:   return config.foxSyncEnabled ? "ON" : "OFF";
    case MENU_BATTERY:    return config.batteryEnabled ? "ON" : "OFF";
    case MENU_BEACON_MODE:return config.beaconMode ? "BEACON" : "FOX";
    case MENU_ECO_MODE:   return config.displayEcoMode ? "ON" : "OFF";
    case MENU_EXIT:       return "";
    default:              return "";
  }
}

// Toggle the selected menu item and save config.
void menuToggle(int idx) {
  if (idx == MENU_EXIT) {
    displayMode = DisplayMode::Status;
    displayModeEnteredAt = millis();
    return;
  }

  {
    ConfigLock lock;
    switch (idx) {
      case MENU_WIFI_AP:     config.wifiApEnabled = !config.wifiApEnabled; break;
      case MENU_WARBLE:      config.warbleEnabled = !config.warbleEnabled; break;
      case MENU_FOX_SYNC:    config.foxSyncEnabled = !config.foxSyncEnabled; break;
      case MENU_BATTERY:     config.batteryEnabled = !config.batteryEnabled; break;
      case MENU_BEACON_MODE: config.beaconMode = !config.beaconMode; break;
      case MENU_ECO_MODE:    config.displayEcoMode = !config.displayEcoMode; break;
    }
  }
  saveConfig();

  if (idx == MENU_WIFI_AP) {
    applyWifiState();
  } else if (idx == MENU_BATTERY) {
    batteryCacheValid = false;
  } else if (idx == MENU_BEACON_MODE && !config.beaconMode &&
             state == BeaconState::ContinuousTransmit) {
    // runContinuousBeacon() notices the change and returns to Idle itself.
  } else if (idx == MENU_BEACON_MODE && !config.beaconMode) {
    enterIdleAfter(config.idleSeconds * 1000UL);
  }
}

void renderMenu() {
  const char *values[MENU_COUNT];
  for (int i = 0; i < MENU_COUNT; i++) {
    values[i] = menuValueStr(i);
  }
  displayMenu(menuIndex, MENU_COUNT, menuLabels, values);
}

const char *stateToString(BeaconState s) {
  switch (s) {
    case BeaconState::StartupDelay: return "STARTUP";
    case BeaconState::Idle: return "IDLE";
    case BeaconState::Transmitting: return "TX";
    case BeaconState::ContinuousTransmit: return "BEACON";
    case BeaconState::LowBatteryHalt: return "LOWBAT";
    default: return "?";
  }
}

void updateDisplay() {
  // Handle startup screen transition
  if (displayMode == DisplayMode::StartupScreen) {
    if (elapsedSince(displayModeEnteredAt) >= STARTUP_SCREEN_MS) {
      displayMode = DisplayMode::Status;
      displayModeEnteredAt = millis();
      lastActivityAt = millis();
    }
    return;
  }

  // Throttle display updates to 1 per second for status, faster for menu
  const uint32_t updateInterval = displayMode == DisplayMode::Menu
                                     ? MENU_UPDATE_INTERVAL_MS
                                     : 1000;
  if (millis() - lastDisplayUpdateAt < updateInterval) return;
  lastDisplayUpdateAt = millis();

  // Eco mode: sleep display after inactivity. During the grace period after
  // boot the status screen keeps drawing so the operator can check it.
  if (displayMode == DisplayMode::Status && config.displayEcoMode) {
    const bool inGracePeriod = millis() < DISPLAY_ECO_GRACE_MS;
    if (!inGracePeriod && !displaySleeping &&
        elapsedSince(lastActivityAt) > DISPLAY_ECO_TIMEOUT_MS) {
      displaySleeping = true;
      displayPower(false);
      return;
    }
    if (displaySleeping) return;
  } else if (displaySleeping) {
    displaySleeping = false;
    displayPower(true);
  }

  // Menu timeout: return to status after inactivity
  if (displayMode == DisplayMode::Menu) {
    if (elapsedSince(lastActivityAt) > MENU_TIMEOUT_MS) {
      displayMode = DisplayMode::Status;
      displayModeEnteredAt = millis();
    } else {
      renderMenu();
      return;
    }
  }

  // Status screen
  char timingStr[32];
  snprintf(timingStr, sizeof(timingStr), "TX:%lus IDLE:%lus",
           static_cast<unsigned long>(config.transmitSeconds),
           static_cast<unsigned long>(config.idleSeconds));

  char batteryStr[20] = "";
  if (config.batteryEnabled) {
    snprintf(batteryStr, sizeof(batteryStr), "%.2fV %u%%",
             readBatteryVoltage(), readBatteryPercent());
  }

  const String ip = webAdminIsRunning() ? webAdminGetIp() : String();
  displayUpdate(config.callSign.c_str(),
                config.foxId.c_str(),
                config.beaconMode ? "BEACON" : "FOX",
                stateToString(state),
                timingStr,
                batteryStr,
                ip.c_str());
}

String normalizeId(String value) {
  value.trim();
  value.toUpperCase();
  String result;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '-') {
      result += c;
    }
  }
  return result.length() ? result : "FOX";
}

// Returns the ARDF fox number (1-5) for a standard fox ID, or 0 if the ID is
// not one of the five standard IARU identifiers.
uint8_t foxNumberFromId(const String &id) {
  const String normalized = normalizeId(id);
  if (normalized == "MOE") return 1;
  if (normalized == "MOI") return 2;
  if (normalized == "MOS") return 3;
  if (normalized == "MOH") return 4;
  if (normalized == "MO5") return 5;
  return 0;
}

// When fox-slot sync is enabled and the fox ID is a standard ARDF identifier,
// the startup delay is overridden so that five beacons powered on at roughly
// the same time fall into the standard round-robin order. Fox 1 starts
// immediately, fox 2 after one TX slot, and so on.
uint32_t resolvedStartupDelaySeconds(const BeaconConfig &cfg) {
  if (!cfg.foxSyncEnabled || cfg.beaconMode) {
    return cfg.startupDelaySeconds;
  }
  const uint8_t foxNumber = foxNumberFromId(cfg.foxId);
  if (foxNumber == 0) {
    return cfg.startupDelaySeconds;
  }
  return static_cast<uint32_t>(foxNumber - 1) * cfg.transmitSeconds;
}

uint32_t elapsedSince(uint32_t startedAt) {
  return millis() - startedAt;
}

// True once millis() has reached the given timestamp (wrap-safe).
bool timeReached(uint32_t at) {
  return static_cast<int32_t>(millis() - at) >= 0;
}

uint32_t cycleMs() {
  return (config.transmitSeconds + config.idleSeconds) * 1000UL;
}

// Move nextTxAt forward by whole cycles until it is in the future. Keeps the
// round-robin phase after a test transmission or a low-battery pause.
void skipMissedSlots() {
  while (static_cast<int32_t>(millis() - nextTxAt) > 0) {
    nextTxAt += cycleMs();
  }
}

void enterIdleAfter(uint32_t delayMs) {
  nextTxAt = millis() + delayMs;
  enterState(BeaconState::Idle);
}

// Called when the startup delay ends (or at boot when there is none).
void finishStartup() {
  if (config.beaconMode) {
    enterState(BeaconState::ContinuousTransmit);
  } else if (config.foxSyncEnabled && foxNumberFromId(config.foxId) != 0) {
    enterIdleAfter(0);  // our slot starts now
  } else {
    enterIdleAfter(config.idleSeconds * 1000UL);
  }
}

void setLed(bool on) {
  ledOn = on;
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
}

void blinkLed(uint32_t intervalMs) {
  if (elapsedSince(lastLedToggleAt) >= intervalMs) {
    lastLedToggleAt = millis();
    setLed(!ledOn);
  }
}

void audioOff() {
  ledcWriteTone(AUDIO_CHANNEL, 0);
  ledcWrite(AUDIO_CHANNEL, 0);
}

void audioTone(uint16_t frequencyHz) {
  ledcWriteTone(AUDIO_CHANNEL, frequencyHz);
  ledcWrite(AUDIO_CHANNEL, AUDIO_DUTY);
}

void setPtt(bool active) {
  const bool outputHigh = config.pttActiveLow ? !active : active;
  digitalWrite(PTT_PIN, outputHigh ? HIGH : LOW);
}

// Estimate state-of-charge (%) for a single-cell Li-ion battery from its
// resting open-circuit voltage. The curve is a piecewise-linear fit to a
// typical 18650 / LiPo discharge profile. For multi-cell packs the scaled
// voltage must fall in the single-cell range (3.2–4.2 V); otherwise the
// percentage is clamped to 0 or 100.
//
// Reference points (V -> %):
//   4.20=100  4.10=90  4.00=80  3.90=70  3.80=55
//   3.70=40   3.60=24  3.50=12  3.40=5   3.30=1  3.20=0
uint8_t batteryPercentFromVoltage(float v) {
  static const struct { float v; uint8_t pct; } pts[] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.90f, 70},
    {3.80f, 55},  {3.70f, 40}, {3.60f, 24}, {3.50f, 12},
    {3.40f, 5},   {3.30f, 1},  {3.20f, 0},
  };
  constexpr uint8_t n = sizeof(pts) / sizeof(pts[0]);

  if (v >= pts[0].v) return 100;
  if (v <= pts[n - 1].v) return 0;

  for (uint8_t i = 1; i < n; ++i) {
    if (v >= pts[i].v) {
      const float dv = pts[i - 1].v - pts[i].v;
      const float dp = pts[i - 1].pct - pts[i].pct;
      return pts[i].pct + static_cast<uint8_t>((v - pts[i].v) / dv * dp + 0.5f);
    }
  }
  return 0;
}

// Average 12 ADC samples (~36 ms). Rate-limited so the main loop, display and
// low-battery check do not hammer the ADC every iteration.
void sampleBatteryIfDue() {
  if (batteryCacheValid && elapsedSince(lastBatterySampleAt) < BATTERY_SAMPLE_INTERVAL_MS) {
    return;
  }
  constexpr uint8_t samples = 12;
  uint32_t totalMillivolts = 0;
  for (uint8_t i = 0; i < samples; ++i) {
    totalMillivolts += analogReadMilliVolts(BATTERY_PIN);
    delay(3);
  }
  const float pinVoltage = (totalMillivolts / static_cast<float>(samples)) / 1000.0f;
  const float voltage = pinVoltage * config.batteryScale;
  batteryVoltageCache = voltage;
  batteryPercentCache = batteryPercentFromVoltage(voltage);
  lastBatterySampleAt = millis();
  batteryCacheValid = true;
}

float readBatteryVoltage() {
  if (!config.batteryEnabled) {
    return 99.0f;
  }
  sampleBatteryIfDue();
  return batteryVoltageCache;
}

uint8_t readBatteryPercent() {
  if (!config.batteryEnabled) return 0;
  sampleBatteryIfDue();
  return batteryPercentCache;
}

bool isLowBattery() {
  return config.batteryEnabled && readBatteryVoltage() <= config.lowBatteryVoltage;
}

const char *morseFor(char c) {
  switch (c) {
    case 'A': return ".-";
    case 'B': return "-...";
    case 'C': return "-.-.";
    case 'D': return "-..";
    case 'E': return ".";
    case 'F': return "..-.";
    case 'G': return "--.";
    case 'H': return "....";
    case 'I': return "..";
    case 'J': return ".---";
    case 'K': return "-.-";
    case 'L': return ".-..";
    case 'M': return "--";
    case 'N': return "-.";
    case 'O': return "---";
    case 'P': return ".--.";
    case 'Q': return "--.-";
    case 'R': return ".-.";
    case 'S': return "...";
    case 'T': return "-";
    case 'U': return "..-";
    case 'V': return "...-";
    case 'W': return ".--";
    case 'X': return "-..-";
    case 'Y': return "-.--";
    case 'Z': return "--..";
    case '0': return "-----";
    case '1': return ".----";
    case '2': return "..---";
    case '3': return "...--";
    case '4': return "....-";
    case '5': return ".....";
    case '6': return "-....";
    case '7': return "--...";
    case '8': return "---..";
    case '9': return "----.";
    case '/': return "-..-.";
    case '-': return "-....-";
    default: return "";
  }
}

void keyedDelay(uint32_t durationMs) {
  const uint32_t startedAt = millis();
  while (elapsedSince(startedAt) < durationMs) {
    delay(1);
  }
}

void sendMorseText(const String &text) {
  const uint32_t dotMs = 1200UL / max<uint16_t>(config.cwWpm, 5);
  const String id = normalizeId(text);

  for (size_t charIndex = 0; charIndex < id.length(); ++charIndex) {
    const char c = id.charAt(charIndex);
    if (c == ' ') {
      keyedDelay(dotMs * 7);
      continue;
    }

    const char *pattern = morseFor(c);
    for (size_t i = 0; pattern[i] != '\0'; ++i) {
      audioTone(config.cwToneHz);
      keyedDelay(pattern[i] == '-' ? dotMs * 3 : dotMs);
      audioOff();
      keyedDelay(dotMs);
    }

    keyedDelay(dotMs * 2);
  }
}

void sendBeaconId() {
  sendMorseText(config.callSign);
  keyedDelay(500);
  sendMorseText(config.foxId);
  keyedDelay(500);
}

void sendWarbleUntil(uint32_t untilMs) {
  bool high = false;
  while (static_cast<int32_t>(untilMs - millis()) > 0) {
    audioTone(high ? config.warbleHighHz : config.warbleLowHz);
    high = !high;
    const uint32_t step = min<uint32_t>(config.warbleStepMs, max<int32_t>(untilMs - millis(), 0));
    keyedDelay(step);
  }
  audioOff();
}

void saveConfig() {
  preferences.begin("foxbeacon", false);
  preferences.putString("call", config.callSign);
  preferences.putString("fox", config.foxId);
  preferences.putUInt("startup", config.startupDelaySeconds);
  preferences.putUInt("tx", config.transmitSeconds);
  preferences.putUInt("idle", config.idleSeconds);
  preferences.putUShort("wpm", config.cwWpm);
  preferences.putUShort("tone", config.cwToneHz);
  preferences.putUShort("warbleLo", config.warbleLowHz);
  preferences.putUShort("warbleHi", config.warbleHighHz);
  preferences.putUShort("warbleMs", config.warbleStepMs);
  preferences.putUShort("lead", config.pttLeadMs);
  preferences.putUShort("tail", config.pttTailMs);
  preferences.putBool("pttLow", config.pttActiveLow);
  preferences.putBool("warble", config.warbleEnabled);
  preferences.putBool("batEn", config.batteryEnabled);
  preferences.putFloat("batScale", config.batteryScale);
  preferences.putFloat("batLow", config.lowBatteryVoltage);
  preferences.putBool("foxSync", config.foxSyncEnabled);
  preferences.putBool("beaconMode", config.beaconMode);
  preferences.putUShort("beaconId", config.beaconIdIntervalSeconds);
  preferences.putBool("wifiAp", config.wifiApEnabled);
  preferences.putUShort("wifiApTo", config.wifiApTimeoutMinutes);
  preferences.putBool("ecoDisp", config.displayEcoMode);
  preferences.end();
}

void loadDefaultConfig() {
  config = BeaconConfig();
  config.callSign = normalizeId(config.callSign);
  config.foxId = normalizeId(config.foxId);
}

void loadConfig() {
  ConfigLock lock;
  loadDefaultConfig();

  preferences.begin("foxbeacon", true);
  config.callSign = normalizeId(preferences.getString("call", config.callSign));
  config.foxId = normalizeId(preferences.getString("fox", config.foxId));
  config.startupDelaySeconds = preferences.getUInt("startup", config.startupDelaySeconds);
  config.transmitSeconds = preferences.getUInt("tx", config.transmitSeconds);
  config.idleSeconds = preferences.getUInt("idle", config.idleSeconds);
  config.cwWpm = preferences.getUShort("wpm", config.cwWpm);
  config.cwToneHz = preferences.getUShort("tone", config.cwToneHz);
  config.warbleLowHz = preferences.getUShort("warbleLo", config.warbleLowHz);
  config.warbleHighHz = preferences.getUShort("warbleHi", config.warbleHighHz);
  config.warbleStepMs = preferences.getUShort("warbleMs", config.warbleStepMs);
  config.pttLeadMs = preferences.getUShort("lead", config.pttLeadMs);
  config.pttTailMs = preferences.getUShort("tail", config.pttTailMs);
  config.pttActiveLow = preferences.getBool("pttLow", config.pttActiveLow);
  config.warbleEnabled = preferences.getBool("warble", config.warbleEnabled);
  config.batteryEnabled = preferences.getBool("batEn", config.batteryEnabled);
  config.batteryScale = preferences.getFloat("batScale", config.batteryScale);
  config.lowBatteryVoltage = preferences.getFloat("batLow", config.lowBatteryVoltage);
  config.foxSyncEnabled = preferences.getBool("foxSync", config.foxSyncEnabled);
  config.beaconMode = preferences.getBool("beaconMode", config.beaconMode);
  config.beaconIdIntervalSeconds = preferences.getUShort("beaconId", config.beaconIdIntervalSeconds);
  config.wifiApEnabled = preferences.getBool("wifiAp", config.wifiApEnabled);
  config.wifiApTimeoutMinutes = preferences.getUShort("wifiApTo", config.wifiApTimeoutMinutes);
  config.displayEcoMode = preferences.getBool("ecoDisp", config.displayEcoMode);
  preferences.end();

  config.startupDelaySeconds = constrain(config.startupDelaySeconds, 0UL, 3600UL);
  config.transmitSeconds = constrain(config.transmitSeconds, 3UL, 600UL);
  config.idleSeconds = constrain(config.idleSeconds, 5UL, 3600UL);
  config.cwWpm = constrain(config.cwWpm, 5, 35);
  config.cwToneHz = constrain(config.cwToneHz, 300, 1800);
  config.warbleLowHz = constrain(config.warbleLowHz, 300, 1800);
  config.warbleHighHz = constrain(config.warbleHighHz, 300, 1800);
  config.warbleStepMs = constrain(config.warbleStepMs, 100, 3000);
  config.pttLeadMs = constrain(config.pttLeadMs, 50, 3000);
  config.pttTailMs = constrain(config.pttTailMs, 50, 3000);
  config.batteryScale = constrain(config.batteryScale, 1.0f, 10.0f);
  config.lowBatteryVoltage = constrain(config.lowBatteryVoltage, 2.5f, 15.0f);
  config.beaconIdIntervalSeconds = constrain(config.beaconIdIntervalSeconds, 10, 600);
  config.wifiApTimeoutMinutes = constrain(config.wifiApTimeoutMinutes, 0, 1440);
}

void printConfig() {
  Serial.println();
  Serial.println(F("ESP32 Fox Hunt Beacon"));
  Serial.println(F("---------------------"));
  Serial.printf("Callsign:       %s\n", config.callSign.c_str());
  Serial.printf("Fox ID:         %s\n", config.foxId.c_str());
  Serial.printf("Mode:           %s\n", config.beaconMode ? "beacon (continuous)" : "fox (scheduled)");
  Serial.printf("Startup delay:  %lu s\n", static_cast<unsigned long>(config.startupDelaySeconds));
  if (config.foxSyncEnabled && !config.beaconMode) {
    const uint8_t foxNumber = foxNumberFromId(config.foxId);
    if (foxNumber > 0) {
      Serial.printf("                (fox sync: slot %u, starts at %lu s)\n",
                    foxNumber,
                    static_cast<unsigned long>(resolvedStartupDelaySeconds(config)));
    } else {
      Serial.println(F("                (fox sync on but fox ID is not a standard ARDF ID)"));
    }
  }
  Serial.printf("Transmit time:  %lu s\n", static_cast<unsigned long>(config.transmitSeconds));
  Serial.printf("Idle time:      %lu s\n", static_cast<unsigned long>(config.idleSeconds));
  if (config.beaconMode) {
    Serial.printf("Beacon ID every:%u s\n", config.beaconIdIntervalSeconds);
  }
  Serial.printf("CW:             %u WPM at %u Hz\n", config.cwWpm, config.cwToneHz);
  Serial.printf("Warble:         %s, %u/%u Hz, %u ms step\n",
                config.warbleEnabled ? "on" : "off",
                config.warbleLowHz,
                config.warbleHighHz,
                config.warbleStepMs);
  Serial.printf("PTT:            GPIO %u, active %s, lead %u ms, tail %u ms\n",
                PTT_PIN,
                config.pttActiveLow ? "LOW" : "HIGH",
                config.pttLeadMs,
                config.pttTailMs);
  Serial.printf("Audio:          GPIO %u\n", AUDIO_PIN);
  Serial.printf("LED/Button:     GPIO %u / GPIO %u\n", LED_PIN, BUTTON_PIN);
  Serial.printf("Battery:        %s, GPIO %u, scale %.3f, cutoff %.2f V",
                config.batteryEnabled ? "on" : "off",
                BATTERY_PIN,
                config.batteryScale,
                config.lowBatteryVoltage);
  if (config.batteryEnabled) {
    Serial.printf(", now %.2f V (%u%%)", readBatteryVoltage(), readBatteryPercent());
  }
  Serial.println();
  Serial.printf("WiFi AP:        %s", config.wifiApEnabled ? "on" : "off");
  if (config.wifiApEnabled && config.wifiApTimeoutMinutes > 0) {
    Serial.printf(" (auto-off %u min)", config.wifiApTimeoutMinutes);
  }
  Serial.println();
  Serial.printf("Display eco:    %s\n", config.displayEcoMode ? "on" : "off");
  Serial.printf("Version:        %s\n", FIRMWARE_VERSION);
  Serial.println();
  Serial.println(F("Commands:"));
  Serial.println(F("  show"));
  Serial.println(F("  test"));
  Serial.println(F("  ptt_test"));
  Serial.println(F("  defaults"));
  Serial.println(F("  reboot"));
  Serial.println(F("  set call EA5KAO"));
  Serial.println(F("  set fox MOE"));
  Serial.println(F("  set mode fox|beacon"));
  Serial.println(F("  set fox_sync on|off"));
  Serial.println(F("  set startup 300"));
  Serial.println(F("  set tx 60"));
  Serial.println(F("  set idle 240"));
  Serial.println(F("  set beacon_id 60"));
  Serial.println(F("  set wpm 12"));
  Serial.println(F("  set tone 700"));
  Serial.println(F("  set warble on|off"));
  Serial.println(F("  set warble_low 700"));
  Serial.println(F("  set warble_high 900"));
  Serial.println(F("  set warble_step 350"));
  Serial.println(F("  set lead 350"));
  Serial.println(F("  set tail 350"));
  Serial.println(F("  set ptt active_low|active_high"));
  Serial.println(F("  set battery on|off"));
  Serial.println(F("  set battery_scale 2.0"));
  Serial.println(F("  set low_battery 3.4"));
  Serial.println(F("  set wifi_ap on|off"));
  Serial.println(F("  set wifi_ap_timeout 10  (0=never)"));
  Serial.println(F("  set eco_mode on|off"));
  Serial.println();
}

void enterState(BeaconState nextState) {
  state = nextState;
  stateStartedAt = millis();
  lastLedToggleAt = millis();
}

// Start or stop the WiFi AP so it matches config.wifiApEnabled.
void applyWifiState() {
  if (config.wifiApEnabled && !webAdminIsRunning()) {
    webAdminInit(AP_NAME_PREFIX);
  } else if (!config.wifiApEnabled && webAdminIsRunning()) {
    webAdminStop();
  }
}

void restoreDefaults() {
  {
    ConfigLock lock;
    loadDefaultConfig();
  }
  saveConfig();
  batteryCacheValid = false;
  applyWifiState();
}

void testPttOnly();
void readSerialCommands();
void checkButton();

// Apply configuration changes and one-shot actions queued by other tasks
// (web admin). Runs only from the main loop.
void processQueuedRequests() {
  bool haveConfig = false;
  BeaconConfig next;
  {
    ConfigLock lock;
    if (pendingConfigValid) {
      next = pendingConfig;
      pendingConfigValid = false;
      haveConfig = true;
    }
  }
  if (haveConfig) {
    {
      ConfigLock lock;
      config = next;
    }
    saveConfig();
    batteryCacheValid = false;
    applyWifiState();
    Serial.println(F("Web admin: settings saved."));
  }

  const uint8_t requests = pendingRequests.exchange(0);
  if (requests & static_cast<uint8_t>(BeaconRequest::Defaults)) {
    restoreDefaults();
    Serial.println(F("Web admin: defaults restored."));
  }
  if (requests & static_cast<uint8_t>(BeaconRequest::Test)) {
    forceTransmit = true;
    Serial.println(F("Web admin: test transmission queued."));
  }
  if (requests & static_cast<uint8_t>(BeaconRequest::PttTest)) {
    testPttOnly();
  }
  if (requests & static_cast<uint8_t>(BeaconRequest::Reboot)) {
    rebootPending = true;
    rebootRequestedAt = millis();
  }
  if (rebootPending && elapsedSince(rebootRequestedAt) >= REBOOT_DELAY_MS) {
    Serial.println(F("Rebooting."));
    Serial.flush();
    setPtt(false);
    ESP.restart();
  }
}

// Background work that is safe while PTT is held with no audio (the steady
// carrier part of a transmission and the gaps in continuous mode).
void serviceBackgroundTasks() {
  checkButton();
  readSerialCommands();
  webAdminLoop();
  processQueuedRequests();
  updateDisplay();
}

// Wait until untilMs while keeping the UI, web admin and serial responsive.
// Slow display refreshes stop shortly before the deadline so the TX window
// is not stretched.
void waitWithService(uint32_t untilMs) {
  while (static_cast<int32_t>(untilMs - millis()) > 0) {
    const int32_t remaining = static_cast<int32_t>(untilMs - millis());
    if (remaining > static_cast<int32_t>(TX_SERVICE_MARGIN_MS)) {
      blinkLed(LED_TX_BLINK_MS);
      serviceBackgroundTasks();
      delay(5);
    } else {
      delay(1);
    }
  }
}

void transmitBeacon() {
  Serial.println(F("TX start"));
  const uint32_t transmissionEndsAt = millis() + (config.transmitSeconds * 1000UL);
  const uint32_t audioEndsAt = transmissionEndsAt - config.pttTailMs;
  setLed(true);
  setPtt(true);
  keyedDelay(config.pttLeadMs);

  sendBeaconId();

  const int32_t remainingAudioMs = static_cast<int32_t>(audioEndsAt - millis());
  if (remainingAudioMs > 0 && config.warbleEnabled) {
    sendWarbleUntil(audioEndsAt);
  } else {
    audioOff();
  }

  // Steady carrier (or tail) until the TX window ends. Audio timing is no
  // longer critical, so keep the display, button and web admin alive.
  waitWithService(transmissionEndsAt);

  audioOff();
  setPtt(false);
  setLed(false);
  Serial.println(F("TX end"));
}

void runContinuousBeacon() {
  Serial.println(F("Continuous beacon start"));
  setLed(true);
  setPtt(true);
  keyedDelay(config.pttLeadMs);

  while (state == BeaconState::ContinuousTransmit && config.beaconMode) {
    const uint32_t nextIdAt = millis() + (config.beaconIdIntervalSeconds * 1000UL);
    audioOff();
    sendBeaconId();
    audioOff();

    while (state == BeaconState::ContinuousTransmit && config.beaconMode &&
           static_cast<int32_t>(nextIdAt - millis()) > 0) {
      if (isLowBattery()) {
        break;
      }
      blinkLed(LED_TX_BLINK_MS);
      serviceBackgroundTasks();
      if (forceTransmit) {
        forceTransmit = false;
        Serial.println(F("Beacon ID retransmission."));
        audioOff();
        sendBeaconId();
        audioOff();
      }
      delay(10);
    }
    if (isLowBattery()) {
      break;
    }
  }

  keyedDelay(config.pttTailMs);
  audioOff();
  setPtt(false);
  setLed(false);
  if (state == BeaconState::ContinuousTransmit && !config.beaconMode) {
    enterIdleAfter(config.idleSeconds * 1000UL);
  }
  Serial.println(F("Continuous beacon end"));
}

void testPttOnly() {
  Serial.println(F("PTT test start"));
  const bool restorePtt = state == BeaconState::Transmitting ||
                          state == BeaconState::ContinuousTransmit;
  audioOff();
  setPtt(true);
  setLed(true);
  keyedDelay(PTT_TEST_MS);
  setPtt(restorePtt);
  setLed(restorePtt);
  Serial.println(F("PTT test end"));
}

bool parseBoolValue(const String &value, bool *out) {
  if (value == "on" || value == "true" || value == "yes" || value == "1") {
    *out = true;
    return true;
  }
  if (value == "off" || value == "false" || value == "no" || value == "0") {
    *out = false;
    return true;
  }
  return false;
}

// Parse one "set <key> <value>" command into config. Returns false (and
// prints usage) if the command was not understood.
bool applySetCommand(String key, String value) {
  key.toLowerCase();
  value.trim();

  ConfigLock lock;
  if (key == "call") {
    config.callSign = normalizeId(value);
  } else if (key == "fox") {
    config.foxId = normalizeId(value);
  } else if (key == "mode") {
    value.toLowerCase();
    if (value == "beacon" || value == "continuous") {
      config.beaconMode = true;
    } else if (value == "fox" || value == "scheduled") {
      config.beaconMode = false;
    } else {
      Serial.println(F("Use: set mode fox|beacon"));
      return false;
    }
  } else if (key == "fox_sync") {
    bool parsed = false;
    if (!parseBoolValue(value, &parsed)) {
      Serial.println(F("Use: set fox_sync on|off"));
      return false;
    }
    config.foxSyncEnabled = parsed;
  } else if (key == "beacon_id") {
    config.beaconIdIntervalSeconds = constrain(value.toInt(), 10, 600);
  } else if (key == "startup") {
    config.startupDelaySeconds = constrain(value.toInt(), 0, 3600);
  } else if (key == "tx") {
    config.transmitSeconds = constrain(value.toInt(), 3, 600);
  } else if (key == "idle") {
    config.idleSeconds = constrain(value.toInt(), 5, 3600);
  } else if (key == "wpm") {
    config.cwWpm = constrain(value.toInt(), 5, 35);
  } else if (key == "tone") {
    config.cwToneHz = constrain(value.toInt(), 300, 1800);
  } else if (key == "warble_low") {
    config.warbleLowHz = constrain(value.toInt(), 300, 1800);
  } else if (key == "warble_high") {
    config.warbleHighHz = constrain(value.toInt(), 300, 1800);
  } else if (key == "warble_step") {
    config.warbleStepMs = constrain(value.toInt(), 100, 3000);
  } else if (key == "lead") {
    config.pttLeadMs = constrain(value.toInt(), 50, 3000);
  } else if (key == "tail") {
    config.pttTailMs = constrain(value.toInt(), 50, 3000);
  } else if (key == "ptt") {
    value.toLowerCase();
    if (value == "active_low") {
      config.pttActiveLow = true;
    } else if (value == "active_high") {
      config.pttActiveLow = false;
    } else {
      Serial.println(F("Use: set ptt active_low|active_high"));
      return false;
    }
  } else if (key == "warble") {
    bool parsed = false;
    if (!parseBoolValue(value, &parsed)) {
      Serial.println(F("Use: set warble on|off"));
      return false;
    }
    config.warbleEnabled = parsed;
  } else if (key == "battery") {
    bool parsed = false;
    if (!parseBoolValue(value, &parsed)) {
      Serial.println(F("Use: set battery on|off"));
      return false;
    }
    config.batteryEnabled = parsed;
  } else if (key == "battery_scale") {
    config.batteryScale = constrain(value.toFloat(), 1.0f, 10.0f);
  } else if (key == "low_battery") {
    config.lowBatteryVoltage = constrain(value.toFloat(), 2.5f, 15.0f);
  } else if (key == "wifi_ap") {
    bool parsed = false;
    if (!parseBoolValue(value, &parsed)) {
      Serial.println(F("Use: set wifi_ap on|off"));
      return false;
    }
    config.wifiApEnabled = parsed;
  } else if (key == "wifi_ap_timeout") {
    config.wifiApTimeoutMinutes = constrain(value.toInt(), 0, 1440);
  } else if (key == "eco_mode") {
    bool parsed = false;
    if (!parseBoolValue(value, &parsed)) {
      Serial.println(F("Use: set eco_mode on|off"));
      return false;
    }
    config.displayEcoMode = parsed;
  } else {
    Serial.println(F("Unknown setting. Type show for command list."));
    return false;
  }
  return true;
}

void handleSetCommand(const String &key, const String &value) {
  if (!applySetCommand(key, value)) {
    return;
  }
  saveConfig();
  batteryCacheValid = false;
  applyWifiState();
  Serial.println(F("Saved."));
  printConfig();
}

void handleCommand(String command) {
  command.trim();
  if (!command.length()) {
    return;
  }

  String lower = command;
  lower.toLowerCase();

  if (lower == "show" || lower == "help" || lower == "?") {
    printConfig();
  } else if (lower == "test") {
    forceTransmit = true;
    Serial.println(F("Test transmission queued."));
  } else if (lower == "ptt_test") {
    testPttOnly();
  } else if (lower == "defaults") {
    restoreDefaults();
    Serial.println(F("Compile-time defaults restored and saved."));
    printConfig();
  } else if (lower == "reboot") {
    Serial.println(F("Rebooting."));
    Serial.flush();
    setPtt(false);
    delay(100);
    ESP.restart();
  } else if (lower.startsWith("set ")) {
    const int firstSpace = command.indexOf(' ');
    const int secondSpace = command.indexOf(' ', firstSpace + 1);
    if (secondSpace < 0) {
      Serial.println(F("Use: set <name> <value>"));
      return;
    }
    String key = command.substring(firstSpace + 1, secondSpace);
    String value = command.substring(secondSpace + 1);
    handleSetCommand(key, value);
  } else {
    Serial.println(F("Unknown command. Type show for help."));
  }
}

void readSerialCommands() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      handleCommand(serialLine);
      serialLine = "";
    } else if (serialLine.length() < 120) {
      serialLine += c;
    }
  }
}

// --- OneButton handlers ---

static void onSingleClick() {
  lastActivityAt = millis();
  if (displaySleeping) {
    displaySleeping = false;
    displayPower(true);
    return;
  }
  if (displayMode == DisplayMode::Menu) {
    menuIndex = (menuIndex + 1) % MENU_COUNT;
  } else if (displayMode == DisplayMode::Status) {
    forceTransmit = true;
    Serial.println(F("Button test transmission queued."));
  }
}

static void onDoubleClick() {
  lastActivityAt = millis();
  if (displaySleeping) {
    displaySleeping = false;
    displayPower(true);
    return;
  }
  if (displayMode == DisplayMode::Menu) {
    menuToggle(menuIndex);
  } else if (displayMode == DisplayMode::Status) {
    displayMode = DisplayMode::Menu;
    displayModeEnteredAt = millis();
    menuIndex = 0;
  }
}

static void onLongPress() {
  lastActivityAt = millis();
  if (displayMode == DisplayMode::Menu) {
    displayMode = DisplayMode::Status;
    displayModeEnteredAt = millis();
  }
}

void checkButton() {
#ifdef BUTTON_PIN
  button.tick();
#endif
}

void setup() {
  configMutex = xSemaphoreCreateMutex();

  // Load saved settings first so the PTT line is driven to the correct idle
  // level for the saved polarity before the pin becomes an output. Otherwise
  // an active-low interface would key the radio during boot.
  loadConfig();
  setPtt(false);
  pinMode(PTT_PIN, OUTPUT);
  setPtt(false);

  pinMode(LED_PIN, OUTPUT);
#ifdef BUTTON_PIN
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  button.attachClick(onSingleClick);
  button.attachDoubleClick(onDoubleClick);
  button.attachLongPressStart(onLongPress);
  button.setPressMs(2000);  // long press = 2 seconds
#endif
  setLed(false);

  Serial.begin(SERIAL_BAUD);
  delay(250);

  displayInit(config.callSign.c_str(), FIRMWARE_VERSION);
  displayMode = DisplayMode::StartupScreen;
  displayModeEnteredAt = millis();

  ledcSetup(AUDIO_CHANNEL, config.cwToneHz, AUDIO_RESOLUTION_BITS);
  ledcAttachPin(AUDIO_PIN, AUDIO_CHANNEL);
  audioOff();

  // Full-scale ~3.1 V at the ADC pin. Set once so the battery monitor can be
  // enabled later from serial, web or menu without a reboot.
  analogSetPinAttenuation(BATTERY_PIN, ADC_11db);

  applyWifiState();

  printConfig();
  Serial.println(F("Beacon armed."));
  if (resolvedStartupDelaySeconds(config) > 0) {
    enterState(BeaconState::StartupDelay);
  } else {
    finishStartup();
  }
}

void loop() {
  readSerialCommands();
  checkButton();
  webAdminLoop();
  processQueuedRequests();

  if (config.beaconMode && state != BeaconState::StartupDelay &&
      state != BeaconState::ContinuousTransmit && state != BeaconState::LowBatteryHalt) {
    enterState(BeaconState::ContinuousTransmit);
  }

  if (state != BeaconState::LowBatteryHalt && isLowBattery()) {
    Serial.printf("Low battery halt: %.2f V\n", readBatteryVoltage());
    audioOff();
    setPtt(false);
    enterState(BeaconState::LowBatteryHalt);
  }

  if (forceTransmit && state != BeaconState::LowBatteryHalt && state != BeaconState::ContinuousTransmit) {
    forceTransmit = false;
    const BeaconState resumeState = state;
    const uint32_t resumeStartedAt = stateStartedAt;
    enterState(BeaconState::Transmitting);
    transmitBeacon();
    if (config.beaconMode) {
      enterState(BeaconState::ContinuousTransmit);
    } else if (resumeState == BeaconState::StartupDelay) {
      // Keep counting the original startup delay so the fox slot is unchanged.
      enterState(BeaconState::StartupDelay);
      stateStartedAt = resumeStartedAt;
    } else {
      // Keep the scheduled slot; skip it only if the test overlapped it.
      skipMissedSlots();
      enterState(BeaconState::Idle);
    }
  }

  switch (state) {
    case BeaconState::StartupDelay: {
      blinkLed(LED_IDLE_BLINK_MS);
      const uint32_t startupDelay = resolvedStartupDelaySeconds(config);
      if (elapsedSince(stateStartedAt) >= startupDelay * 1000UL) {
        finishStartup();
      }
      break;
    }

    case BeaconState::Idle:
      blinkLed(LED_IDLE_BLINK_MS);
      if (timeReached(nextTxAt)) {
        enterState(BeaconState::Transmitting);
      }
      break;

    case BeaconState::Transmitting:
      // Advance from the ideal slot start, not from "now", so loop latency
      // does not accumulate into slot drift across a long event.
      nextTxAt += cycleMs();
      transmitBeacon();
      skipMissedSlots();
      enterState(BeaconState::Idle);
      break;

    case BeaconState::ContinuousTransmit:
      runContinuousBeacon();
      if (isLowBattery()) {
        enterState(BeaconState::LowBatteryHalt);
      }
      break;

    case BeaconState::LowBatteryHalt:
      blinkLed(LED_LOW_BATTERY_BLINK_MS);
      // Resume when monitoring is disabled or the battery has clearly
      // recovered (hysteresis avoids toggling around the cutoff).
      if (!config.batteryEnabled ||
          readBatteryVoltage() > config.lowBatteryVoltage + LOW_BATTERY_RECOVERY_HYSTERESIS_V) {
        Serial.println(F("Battery OK, resuming."));
        skipMissedSlots();
        enterState(BeaconState::Idle);
      }
      break;
  }

  updateDisplay();
  delay(5);
}

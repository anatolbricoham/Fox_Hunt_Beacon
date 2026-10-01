#include "competition.h"

#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>

#include "lora_network.h"

extern BeaconConfig config;

namespace {

constexpr uint32_t NFC_DEBOUNCE_MS = 2000;
constexpr uint32_t MQTT_RETRY_MS = 5000;
constexpr uint32_t WIFI_RETRY_MS = 10000;
constexpr uint8_t MQTT_QUEUE_BATCH_SIZE = 2;

MFRC522 rfid(NFC_SDA_PIN, NFC_RST_PIN);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Preferences competitionPreferences;

String lastTagSeen;
String configuredMqttBroker;
String configuredMqttUser;
String configuredMqttPassword;
String configuredMqttClientId;
uint32_t lastTagSeenAt = 0;
uint32_t lastMqttRetryAt = 0;
uint32_t lastWiFiAttemptAt = 0;
uint16_t configuredMqttPort = 0;
bool stationConnectRequested = false;
bool mqttServerConfigured = false;
bool nfcReaderInitialized = false;

String splitAndTrim(String value, char delimiter, size_t index) {
  int start = 0;
  int end = -1;
  int found = 0;
  for (size_t i = 0; i <= value.length(); ++i) {
    if (value.charAt(i) == delimiter || i == value.length()) {
      if (found == index) {
        end = static_cast<int>(i);
        break;
      }
      start = static_cast<int>(i) + 1;
      ++found;
    }
  }
  if (end < 0) {
    if (index == found) {
      end = static_cast<int>(value.length());
    } else {
      return "";
    }
  }
  String part = value.substring(start, end);
  part.trim();
  return part;
}

String normalizeTagListEntry(const String &entry) {
  String tag = entry;
  tag.trim();
  tag.toUpperCase();
  String out;
  for (size_t i = 0; i < tag.length(); ++i) {
    const char c = tag.charAt(i);
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || c == ':' || c == '-') {
      out += c;
    }
  }
  return out;
}

String escapeJsonString(const String &value) {
  String escaped;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    if (c == '"' || c == '\\') {
      escaped += '\\';
      escaped += c;
    } else if (c == '\n') {
      escaped += F("\\n");
    } else if (c == '\r') {
      escaped += F("\\r");
    } else if (c == '\t') {
      escaped += F("\\t");
    } else if (static_cast<uint8_t>(c) >= 0x20) {
      escaped += c;
    }
  }
  return escaped;
}

String readQueuedEvents() {
  competitionPreferences.begin("nfcq", true);
  const String queue = competitionPreferences.getString("events", "");
  competitionPreferences.end();
  return queue;
}

void saveQueuedEvents(const String &queue) {
  competitionPreferences.begin("nfcq", false);
  competitionPreferences.putString("events", queue);
  competitionPreferences.end();
}

void enqueueEvent(const String &event) {
  String queue = readQueuedEvents();
  if (!queue.isEmpty()) {
    queue += "\n";
  }
  queue += event;
  saveQueuedEvents(queue);
}

String takeQueuedEvent() {
  String queue = readQueuedEvents();
  if (queue.isEmpty()) {
    return "";
  }
  int newlineIndex = queue.indexOf('\n');
  String first = newlineIndex >= 0 ? queue.substring(0, newlineIndex) : queue;
  String remaining = newlineIndex >= 0 ? queue.substring(newlineIndex + 1) : "";
  saveQueuedEvents(remaining);
  return first;
}

void beginWiFiStation() {
  if (config.wifiStationSsid.isEmpty()) {
    return;
  }
  WiFi.mode(WIFI_AP_STA);
  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == config.wifiStationSsid) {
    stationConnectRequested = false;
    return;
  }
  const uint32_t now = millis();
  if (stationConnectRequested && now - lastWiFiAttemptAt < WIFI_RETRY_MS) {
    return;
  }
  lastWiFiAttemptAt = now;
  stationConnectRequested = true;
  WiFi.begin(config.wifiStationSsid.c_str(), config.wifiStationPassword.c_str());
}

bool configureMqttClient() {
  if (!config.mqttEnabled || config.mqttBroker.isEmpty()) {
    if (mqttClient.connected()) {
      mqttClient.disconnect();
    }
    mqttServerConfigured = false;
    return false;
  }

  if (!mqttServerConfigured || configuredMqttBroker != config.mqttBroker ||
      configuredMqttPort != config.mqttPort || configuredMqttUser != config.mqttUser ||
      configuredMqttPassword != config.mqttPassword ||
      configuredMqttClientId != config.participantName) {
    if (mqttClient.connected()) {
      mqttClient.disconnect();
    }
    configuredMqttBroker = config.mqttBroker;
    configuredMqttPort = config.mqttPort;
    configuredMqttUser = config.mqttUser;
    configuredMqttPassword = config.mqttPassword;
    configuredMqttClientId = config.participantName;
    mqttClient.setServer(configuredMqttBroker.c_str(), configuredMqttPort);
    mqttClient.setSocketTimeout(2);
    mqttServerConfigured = true;
  }
  return true;
}

bool connectMqtt() {
  if (!configureMqttClient() || !WiFi.isConnected()) {
    return false;
  }
  if (mqttClient.connected()) {
    return true;
  }
  bool connected = false;
  if (config.mqttUser.isEmpty() && config.mqttPassword.isEmpty()) {
    connected = mqttClient.connect(config.participantName.c_str());
  } else {
    connected = mqttClient.connect(config.participantName.c_str(),
                                   config.mqttUser.c_str(),
                                   config.mqttPassword.c_str());
  }
  mqttClient.setKeepAlive(60);
  return connected;
}

void initializeNfcReader() {
  if (!config.nfcEnabled || nfcReaderInitialized) {
    return;
  }
  SPI.begin(NFC_SCK_PIN, NFC_MISO_PIN, NFC_MOSI_PIN, NFC_SDA_PIN);
  rfid.PCD_Init();
  rfid.PCD_SetAntennaGain(rfid.RxGain_max);
  nfcReaderInitialized = true;
  Serial.printf("NFC reader init on SDA=%d, RST=%d, MQTT=%s\n", NFC_SDA_PIN, NFC_RST_PIN,
                config.mqttEnabled ? "enabled" : "disabled");
}

void flushQueueToMqtt() {
  if (!config.mqttEnabled || config.mqttBroker.isEmpty()) {
    return;
  }
  String published = "";
  for (uint8_t batchIndex = 0; batchIndex < MQTT_QUEUE_BATCH_SIZE; ++batchIndex) {
    const String event = takeQueuedEvent();
    if (event.isEmpty()) {
      break;
    }
    if (!mqttClient.connected()) {
      enqueueEvent(event);
      break;
    }
    const String topic = config.mqttTopic + "/events";
    if (!mqttClient.publish(topic.c_str(), event.c_str())) {
      enqueueEvent(event);
      break;
    }
    published += event;
  }
  if (!published.isEmpty()) {
    Serial.printf("NFC MQTT queue flushed: %s\n", published.c_str());
  }
}

bool publishValidatedTag(const String &tag, const String &player) {
  if (!config.mqttEnabled || config.mqttBroker.isEmpty()) {
    return false;
  }

  String payload = String("{") +
        "\"player\":\"" + escapeJsonString(player) + "\"," +
      "\"tag\":\"" + tag + "\"," +
      "\"status\":\"validated\"," +
      "\"device\":\"" + config.callSign + "\"," +
      "\"fox\":\"" + config.foxId + "\"," +
      "\"timestamp\":\"" + String(millis()) + "\"}";

  if (!mqttClient.connected()) {
    enqueueEvent(payload);
    return true;
  }

  if (mqttClient.publish((config.mqttTopic + "/validated").c_str(), payload.c_str())) {
    Serial.printf("NFC validation published to MQTT: %s\n", payload.c_str());
    return true;
  } else {
    enqueueEvent(payload);
    return true;
  }
}

String serializeTagList(const String &value) {
  String out;
  int pos = 0;
  while (pos <= value.length()) {
    int next = pos;
    while (next < value.length() && value.charAt(next) != ',' && value.charAt(next) != ';') {
      ++next;
    }
    String item = value.substring(pos, next);
    item = normalizeTagListEntry(item);
    if (!item.isEmpty()) {
      if (!out.isEmpty()) {
        out += ";";
      }
      out += item;
    }
    if (next >= value.length()) {
      break;
    }
    pos = next + 1;
  }
  return out;
}

}  // namespace

String competitionNormalizeTag(const uint8_t *uid, uint8_t uidLength) {
  String out;
  for (uint8_t i = 0; i < uidLength; ++i) {
    if (out.length() > 0) {
      out += ":";
    }
    char hex[4];
    snprintf(hex, sizeof(hex), "%02X", uid[i]);
    out += hex;
  }
  return out;
}

bool competitionTagIsAllowed(const String &tag) {
  String whitelist = config.nfcAllowedTags;
  whitelist = serializeTagList(whitelist);
  if (whitelist.isEmpty()) {
    return true;
  }
  String candidate = normalizeTagListEntry(tag);
  if (candidate.isEmpty()) {
    return false;
  }

  int offset = 0;
  while (offset <= whitelist.length()) {
    int nextSeparator = whitelist.indexOf(';', offset);
    String item = nextSeparator >= 0 ? whitelist.substring(offset, nextSeparator) : whitelist.substring(offset);
    item.trim();
    if (item == candidate) {
      return true;
    }
    if (nextSeparator < 0) {
      break;
    }
    offset = nextSeparator + 1;
  }
  return false;
}

bool competitionPublishAuthenticatedTag(const String &tag, const String &player) {
  return publishValidatedTag(tag, player);
}

void competitionInit() {
  if (!config.nfcEnabled && !config.mqttEnabled) {
    return;
  }

  initializeNfcReader();
  if (config.mqttEnabled && !config.wifiStationSsid.isEmpty()) {
    beginWiFiStation();
  }
  if (config.mqttEnabled) {
    connectMqtt();
  }
}

void competitionLoop() {
  configureMqttClient();
  if (!config.nfcEnabled && !config.mqttEnabled) {
    return;
  }

  if (config.mqttEnabled && !config.wifiStationSsid.isEmpty() &&
      (WiFi.status() != WL_CONNECTED || WiFi.SSID() != config.wifiStationSsid)) {
    beginWiFiStation();
  }
  if (config.mqttEnabled && !config.mqttBroker.isEmpty() &&
      (!mqttServerConfigured || configuredMqttBroker != config.mqttBroker ||
       configuredMqttPort != config.mqttPort)) {
    connectMqtt();
  }

  if (config.mqttEnabled && config.mqttBroker.length() > 0 && WiFi.isConnected()) {
    if (!mqttClient.connected()) {
      if (millis() - lastMqttRetryAt >= MQTT_RETRY_MS) {
        lastMqttRetryAt = millis();
        connectMqtt();
      }
    } else {
      mqttClient.loop();
    }
    flushQueueToMqtt();
  }

  if (!config.nfcEnabled) {
    return;
  }
  initializeNfcReader();

  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  const String tag = competitionNormalizeTag(rfid.uid.uidByte, rfid.uid.size);
  const bool isAllowed = competitionTagIsAllowed(tag);
  const uint32_t now = millis();

  if (lastTagSeen == tag && now - lastTagSeenAt < NFC_DEBOUNCE_MS) {
    rfid.PICC_HaltA();
    return;
  }
  lastTagSeen = tag;
  lastTagSeenAt = now;

  if (isAllowed) {
    Serial.printf("NFC tag validated: %s (%s)\n", tag.c_str(), config.participantName.c_str());
    if (config.loraEnabled && !config.loraMasterMode &&
        loraNetworkPublish("NFC", tag + ";" + config.participantName)) {
      Serial.println(F("NFC validation queued for LoRa master acknowledgement."));
    } else {
      publishValidatedTag(tag, config.participantName);
    }
  } else {
    Serial.printf("NFC tag rejected: %s\n", tag.c_str());
  }

  rfid.PICC_HaltA();
}

void competitionFlushPendingEvents() {
  if (!config.mqttEnabled || !mqttClient.connected()) {
    return;
  }
  flushQueueToMqtt();
}

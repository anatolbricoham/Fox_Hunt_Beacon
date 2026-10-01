#include "lora_network.h"

#include <LoRa.h>
#include <SPI.h>
#include <esp_system.h>
#include <stdlib.h>

#include "beacon_config.h"
#include "competition.h"

extern BeaconConfig config;

namespace {

constexpr uint32_t MASTER_SYNC_INTERVAL_MS = 10000;
constexpr uint32_t SYNC_TIMEOUT_MS = 30000;
constexpr uint32_t SLAVE_HELLO_MIN_MS = 30000;
constexpr uint32_t SLAVE_HELLO_JITTER_MS = 15000;
constexpr uint32_t EVENT_ACK_TIMEOUT_MS = 1500;
constexpr uint8_t EVENT_MAX_RETRIES = 5;
constexpr size_t EVENT_QUEUE_CAPACITY = 4;
constexpr size_t EVENT_HISTORY_CAPACITY = 12;

struct SeenEvent {
  String sender;
  uint32_t id = 0;
};

bool initialized = false;
bool timeSynchronized = false;
uint32_t lastSyncTxAt = 0;
uint32_t lastSyncReceivedAt = 0;
uint32_t lastHelloTxAt = 0;
uint32_t nextHelloIntervalMs = SLAVE_HELLO_MIN_MS;
uint32_t timeOffsetSeconds = 0;
String eventQueue[EVENT_QUEUE_CAPACITY];
uint8_t eventQueueHead = 0;
uint8_t eventQueueCount = 0;
uint32_t activeEventId = 0;
uint8_t activeEventRetries = 0;
uint32_t lastEventTxAt = 0;
uint32_t nextEventTxAt = 0;
bool activeEventSent = false;
SeenEvent seenEvents[EVENT_HISTORY_CAPACITY];
size_t seenEventCursor = 0;

uint32_t randomBetween(uint32_t minimum, uint32_t maximumExclusive) {
  return minimum + (esp_random() % (maximumExclusive - minimum));
}

String nextPart(const String &value, char delimiter, size_t index) {
  int start = 0;
  int end = -1;
  int found = 0;
  for (size_t i = 0; i <= value.length(); ++i) {
    if (i == value.length() || value.charAt(i) == delimiter) {
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
  return value.substring(start, end);
}

String cleanField(const String &value) {
  String out;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    if (c == '|' || c == '\n' || c == '\r') {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

uint32_t parseUnsigned(const String &value) {
  return static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
}

bool wasEventSeen(const String &sender, uint32_t eventId) {
  for (const SeenEvent &event : seenEvents) {
    if (event.id == eventId && event.sender == sender) {
      return true;
    }
  }
  return false;
}

void rememberEvent(const String &sender, uint32_t eventId) {
  seenEvents[seenEventCursor].sender = sender;
  seenEvents[seenEventCursor].id = eventId;
  seenEventCursor = (seenEventCursor + 1) % EVENT_HISTORY_CAPACITY;
}

bool sendFrame(const String &type, const String &payload) {
  if (!initialized) {
    return false;
  }

  const String frame = type + "|" + cleanField(config.loraNodeId) + "|" + payload;
  if (frame.length() > 240) {
    Serial.println(F("LoRa frame rejected: exceeds packet limit."));
    return false;
  }

  LoRa.beginPacket();
  LoRa.print(frame);
  return LoRa.endPacket() == 1;
}

void removeQueuedEvent() {
  if (eventQueueCount == 0) {
    return;
  }
  eventQueue[eventQueueHead] = "";
  eventQueueHead = (eventQueueHead + 1) % EVENT_QUEUE_CAPACITY;
  --eventQueueCount;
  activeEventId = 0;
  activeEventRetries = 0;
  activeEventSent = false;
  lastEventTxAt = 0;
  nextEventTxAt = millis() + randomBetween(100, 500);
}

void sendEventAcknowledgement(const String &targetNode, uint32_t eventId) {
  sendFrame("ACK", cleanField(targetNode) + "|" + String(eventId));
}

void dispatchReceivedFrame(const String &frame) {
  if (frame.length() < 3) {
    return;
  }

  const String type = nextPart(frame, '|', 0);
  const String sender = nextPart(frame, '|', 1);
  const String payload = nextPart(frame, '|', 2);

  if (type == "SYNC" && !config.loraMasterMode && !sender.isEmpty()) {
    const uint32_t masterSeconds = parseUnsigned(payload);
    timeOffsetSeconds = masterSeconds - (millis() / 1000UL);
    timeSynchronized = true;
    lastSyncReceivedAt = millis();
    Serial.printf("LoRa time sync from %s: network uptime %lu s\n",
                  sender.c_str(), static_cast<unsigned long>(masterSeconds));
    return;
  }

  if (type == "EVT") {
    if (!config.loraMasterMode || sender.isEmpty()) {
      return;
    }

    const uint32_t eventId = parseUnsigned(nextPart(frame, '|', 2));
    const String kind = nextPart(frame, '|', 3);
    const String eventPayload = nextPart(frame, '|', 4);
    const String tag = nextPart(eventPayload, ';', 0);
    const String player = nextPart(eventPayload, ';', 1);
    if (eventId == 0 || kind != "NFC" || tag.isEmpty() || player.isEmpty()) {
      Serial.printf("LoRa event rejected from %s: malformed frame.\n", sender.c_str());
      return;
    }

    if (wasEventSeen(sender, eventId)) {
      sendEventAcknowledgement(sender, eventId);
      return;
    }

    if (competitionPublishAuthenticatedTag(tag, player)) {
      rememberEvent(sender, eventId);
      sendEventAcknowledgement(sender, eventId);
      Serial.printf("LoRa NFC event accepted from %s: %s / %s\n",
                    sender.c_str(), tag.c_str(), player.c_str());
    } else {
      Serial.printf("LoRa NFC event from %s not acknowledged: MQTT is disabled or unconfigured.\n",
                    sender.c_str());
    }
    return;
  }

  if (type == "ACK" && !config.loraMasterMode) {
    const String targetNode = nextPart(frame, '|', 2);
    const uint32_t eventId = parseUnsigned(nextPart(frame, '|', 3));
    if (targetNode == config.loraNodeId && eventQueueCount > 0 && eventId == activeEventId) {
      Serial.printf("LoRa event %lu acknowledged by %s.\n",
                    static_cast<unsigned long>(eventId), sender.c_str());
      removeQueuedEvent();
    }
    return;
  }

  if (type == "HELLO" && config.loraMasterMode) {
    Serial.printf("LoRa hello from %s\n", sender.c_str());
  }
}

void serviceEventQueue() {
  if (!initialized || config.loraMasterMode || eventQueueCount == 0) {
    return;
  }

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextEventTxAt) < 0) {
    return;
  }
  if (activeEventSent && now - lastEventTxAt < EVENT_ACK_TIMEOUT_MS) {
    return;
  }
  if (activeEventSent && activeEventRetries >= EVENT_MAX_RETRIES) {
    Serial.printf("LoRa event %lu dropped after %u retries without ACK.\n",
                  static_cast<unsigned long>(activeEventId), activeEventRetries);
    removeQueuedEvent();
    return;
  }

  if (!activeEventSent) {
    activeEventId = esp_random();
    if (activeEventId == 0) {
      activeEventId = 1;
    }
  } else {
    ++activeEventRetries;
  }

  const String queued = eventQueue[eventQueueHead];
  const String kind = nextPart(queued, '|', 0);
  const String payload = nextPart(queued, '|', 1);
  const bool sent = sendFrame("EVT", String(activeEventId) + "|" + kind + "|" + payload);
  activeEventSent = true;
  lastEventTxAt = now;
  nextEventTxAt = now + EVENT_ACK_TIMEOUT_MS + randomBetween(0, 500);
  if (!sent) {
    Serial.println(F("LoRa event transmission failed; retry scheduled."));
  }
}

}  // namespace

void loraNetworkInit() {
  if (!config.loraEnabled) {
    return;
  }

  LoRa.setSPI(SPI);
  SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_CS_PIN);
  LoRa.setPins(LORA_CS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
  if (!LoRa.begin(config.loraFrequencyHz / 1000000.0)) {
    Serial.println(F("LoRa init failed. Check pins and board support."));
    return;
  }

  LoRa.setTxPower(config.loraTxPowerDbm);
  LoRa.setSyncWord(config.loraSyncWord);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  initialized = true;
  lastSyncTxAt = millis();
  lastHelloTxAt = millis();
  nextHelloIntervalMs = randomBetween(SLAVE_HELLO_MIN_MS,
                                      SLAVE_HELLO_MIN_MS + SLAVE_HELLO_JITTER_MS);
  Serial.printf("LoRa network enabled: role=%s node=%s freq=%lu Hz\n",
                config.loraMasterMode ? "master" : "slave",
                config.loraNodeId.c_str(),
                static_cast<unsigned long>(config.loraFrequencyHz));
}

void loraNetworkLoop() {
  if (!config.loraEnabled || !initialized) {
    return;
  }

  int packetSize = LoRa.parsePacket();
  if (packetSize > 0) {
    String frame;
    while (LoRa.available()) {
      frame += static_cast<char>(LoRa.read());
    }
    if (!frame.isEmpty()) {
      dispatchReceivedFrame(frame);
    }
  }

  const uint32_t nowMs = millis();
  if (config.loraMasterMode) {
    if (nowMs - lastSyncTxAt >= MASTER_SYNC_INTERVAL_MS) {
      lastSyncTxAt = nowMs;
      sendFrame("SYNC", String(nowMs / 1000UL));
    }
  } else {
    if (nowMs - lastHelloTxAt >= nextHelloIntervalMs) {
      lastHelloTxAt = nowMs;
      nextHelloIntervalMs = randomBetween(SLAVE_HELLO_MIN_MS,
                  SLAVE_HELLO_MIN_MS + SLAVE_HELLO_JITTER_MS);
      sendFrame("HELLO", "");
    }
  }

  serviceEventQueue();
}

bool loraNetworkPublish(const String &kind, const String &payload) {
  if (!config.loraEnabled || !initialized || config.loraMasterMode) {
    return false;
  }
  if (eventQueueCount >= EVENT_QUEUE_CAPACITY) {
    Serial.println(F("LoRa event queue full; event not queued."));
    return false;
  }

  eventQueue[(eventQueueHead + eventQueueCount) % EVENT_QUEUE_CAPACITY] =
      cleanField(kind) + "|" + cleanField(payload);
  ++eventQueueCount;
  if (eventQueueCount == 1) {
    nextEventTxAt = millis() + randomBetween(100, 500);
  }
  return true;
}

uint32_t loraNetworkCurrentTime() {
  if (!loraNetworkTimeIsSynchronized()) {
    return 0;
  }
  return static_cast<uint32_t>(millis() / 1000UL) + timeOffsetSeconds;
}

bool loraNetworkTimeIsSynchronized() {
  return initialized && (config.loraMasterMode ||
                         (timeSynchronized && millis() - lastSyncReceivedAt <= SYNC_TIMEOUT_MS));
}

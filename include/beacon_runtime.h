#pragma once

#include <Arduino.h>
#include "beacon_config.h"

// Bridge between the beacon main loop (src/main.cpp) and code that runs in
// other FreeRTOS tasks, such as the ESPAsyncWebServer handlers in
// src/web_admin.cpp.
//
// Rules:
//   - Only the main loop writes the live BeaconConfig.
//   - Other tasks read a copy via beaconConfigSnapshot() and submit changes
//     with beaconQueueConfig() / beaconQueueRequest(). The main loop applies
//     them at a safe point (never in the middle of a CW element).

// Actions another task can ask the main loop to perform.
enum class BeaconRequest : uint8_t {
  Test = 1 << 0,      // queue one immediate transmission
  PttTest = 1 << 1,   // key PTT for ~1.2 s with no audio
  Defaults = 1 << 2,  // restore compile-time defaults and save them
  Reboot = 1 << 3,    // restart the ESP32 shortly after the HTTP reply
};

// Returns a consistent copy of the current configuration. Safe from any task.
BeaconConfig beaconConfigSnapshot();

// Replace the whole configuration. Applied and saved by the main loop.
void beaconQueueConfig(const BeaconConfig &next);

// Queue a one-shot action for the main loop. Safe from any task.
void beaconQueueRequest(BeaconRequest request);

// Current BeaconState as an int. Safe from any task.
int beaconStateValue();

// Last battery measurement taken by the main loop (no ADC access).
float beaconBatteryVoltage();
uint8_t beaconBatteryPercent();

// Helpers shared with the web admin.
String normalizeId(String value);
uint8_t foxNumberFromId(const String &id);
uint32_t resolvedStartupDelaySeconds(const BeaconConfig &cfg);

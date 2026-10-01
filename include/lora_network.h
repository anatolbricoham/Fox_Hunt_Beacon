#pragma once

#include <Arduino.h>

void loraNetworkInit();
void loraNetworkLoop();
bool loraNetworkPublish(const String &kind, const String &payload);
uint32_t loraNetworkCurrentTime();
bool loraNetworkTimeIsSynchronized();

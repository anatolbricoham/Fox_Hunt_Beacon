#pragma once

#include <Arduino.h>
#include "beacon_config.h"

void competitionInit();
void competitionLoop();
void competitionFlushPendingEvents();
String competitionNormalizeTag(const uint8_t *uid, uint8_t uidLength);
bool competitionTagIsAllowed(const String &tag);
bool competitionPublishAuthenticatedTag(const String &tag, const String &player);

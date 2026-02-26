#pragma once

#include <Arduino.h>

void fwWatchdogInit(uint32_t timeoutMs);
void fwWatchdogFeed();


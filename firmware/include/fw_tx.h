#pragma once

#include <Arduino.h>

void txInit();
void txService();
size_t txQueuedBytes();
size_t txFreeBytes();
bool txWriteBytes(const uint8_t* data, size_t len);
bool txWriteByte(uint8_t b);
bool txWriteCString(const char* s);


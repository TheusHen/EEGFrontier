#pragma once

#include "fw_state.h"

void printHelp();
void printInfo();
void printStats();
void dumpRegisters();
void processCommand(char* cmd);
void handleSerialCommands();
void handleButton();
void onDrdyFalling();
bool capturePendingDrdySnapshot(DrdyFrameSnapshot* out);
void captureDrdyJitterSnapshot(DrdyJitterSnapshot* out);

#pragma once

#include <Arduino.h>

void adsSendCommand(uint8_t cmd);
uint8_t adsReadRegister(uint8_t reg);
void adsWriteRegister(uint8_t reg, uint8_t value);
void adsReadRegisters(uint8_t startReg, uint8_t count, uint8_t* dest);
// Register access is only valid outside RDATAC. These helpers stop the
// stream first when needed and report whether they had to.
bool adsEnsureIdleForRegAccess(bool* wasStreaming);

void adsHardwareReset();
bool adsConfigureRegisters();
bool adsInitOnce();
bool adsInitRobust(uint8_t attempts = 3);

void adsStartStreaming();
void adsStopStreaming();
bool adsReadDataFrame15(uint8_t* frame);

bool handleOneSampleFrame();
void recoverAdsIfNeeded();

bool adsSetInternalTestSignal(bool enable);
bool adsRunInternalSelfTest(uint8_t frames = 32);
bool adsSetLeadOffDiagnostics(bool enable);
bool adsSetSampleRate(uint32_t sps);
bool adsSetGain(uint8_t gain);
bool adsSetVrefUv(uint32_t vrefUv);
int32_t adsCountsToMicrovolts(int32_t counts);

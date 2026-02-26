#include "fw_state.h"

OutputMode g_outputMode = MODE_BIN;

SPISettings g_spiSettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE1);

volatile bool g_drdyFlag = false;
volatile uint32_t g_missedDrdyTotal = 0;
volatile uint32_t g_missedDrdyFrame = 0;
volatile uint32_t g_drdyEdgesTotal = 0;
volatile uint32_t g_lastDrdyTimestampUs = 0;
volatile uint32_t g_prevDrdyTimestampUs = 0;
volatile uint32_t g_drdyIntervalLastUs = 0;
volatile uint32_t g_drdyIntervalMinUs = 0xFFFFFFFFUL;
volatile uint32_t g_drdyIntervalMaxUs = 0;
volatile uint32_t g_drdyJitterAbsLastUs = 0;
volatile uint32_t g_drdyJitterAbsMinUs = 0xFFFFFFFFUL;
volatile uint32_t g_drdyJitterAbsMaxUs = 0;
volatile uint32_t g_drdyIntervalCount = 0;
volatile uint64_t g_drdyIntervalSumUs = 0;
volatile uint64_t g_drdyJitterAbsSumUs = 0;

bool g_streaming = false;
bool g_adsReady = false;
bool g_pendingRecoveredFlag = false;
bool g_pendingBtnFlag = false;
bool g_pendingTxOverflowFlag = false;

uint32_t g_sampleIndex = 0;
uint32_t g_recoveriesTotal = 0;
uint32_t g_lastGoodFrameUs = 0;
uint32_t g_lastButtonToggleMs = 0;
uint32_t g_lastSampleProcessUs = 0;
uint32_t g_lastDrdyToProcessLatencyUs = 0;

bool g_lastBtnState = HIGH;

uint8_t g_rawFrame[15] = {0};

char g_cmdBuf[96] = {0};
size_t g_cmdLen = 0;

uint32_t g_sampleRateSps = ADS_DEFAULT_SPS;
uint8_t g_adsGain = ADS_DEFAULT_GAIN;
uint32_t g_adsVrefUv = ADS_VREF_UV;
bool g_internalTestSignalEnabled = false;
bool g_leadOffDiagEnabled = false;
uint8_t g_lastLeadOffStatP = 0;
uint8_t g_lastLeadOffStatN = 0;
uint32_t g_lastStatus24 = 0;
uint32_t g_statusInvalidTotal = 0;
uint32_t g_leadOffAnyTotal = 0;

uint32_t g_txBytesDroppedTotal = 0;
uint32_t g_txPacketsDroppedTotal = 0;
uint32_t g_txMaxQueuedBytes = 0;

bool g_watchdogSupported = false;
bool g_watchdogEnabled = false;
bool g_watchdogRebootDetected = false;
uint32_t g_watchdogTimeoutMs = 0;
uint32_t g_watchdogFeedsTotal = 0;
uint32_t g_watchdogLastFeedMs = 0;

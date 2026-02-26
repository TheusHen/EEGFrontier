#include "fw_watchdog.h"

#include "fw_state.h"

#if defined(__has_include)
#if __has_include(<hardware/watchdog.h>)
#include <hardware/watchdog.h>
#define FW_HAS_PICO_WATCHDOG 1
#else
#define FW_HAS_PICO_WATCHDOG 0
#endif
#else
#define FW_HAS_PICO_WATCHDOG 0
#endif

void fwWatchdogInit(uint32_t timeoutMs) {
  g_watchdogTimeoutMs = timeoutMs;

#if FW_HAS_PICO_WATCHDOG
  g_watchdogSupported = true;
  g_watchdogRebootDetected = watchdog_caused_reboot();
  watchdog_enable(timeoutMs, true);
  g_watchdogEnabled = true;
  g_watchdogLastFeedMs = millis();
  g_watchdogFeedsTotal = 0;
#else
  (void)timeoutMs;
  g_watchdogSupported = false;
  g_watchdogEnabled = false;
  g_watchdogRebootDetected = false;
#endif
}

void fwWatchdogFeed() {
  if (!g_watchdogEnabled) {
    return;
  }

#if FW_HAS_PICO_WATCHDOG
  watchdog_update();
#endif
  g_watchdogFeedsTotal++;
  g_watchdogLastFeedMs = millis();
}


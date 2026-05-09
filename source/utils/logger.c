#include "logger.h"
#include <coreinit/debug.h>
#include <whb/log.h>
#include <whb/log_udp.h>
#include <whb/log_cafe.h>

void initLogging() {
    WHBLogUdpInit();
    WHBLogCafeInit();
    WHBLogPrint("Logging initialized (UDP + Cafe console)");
}

void deinitLogging() {
    WHBLogPrint("Logging shutting down");
    WHBLogUdpDeinit();
    WHBLogCafeDeinit();
}

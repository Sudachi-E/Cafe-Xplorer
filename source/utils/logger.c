#include "logger.h"
#include <coreinit/debug.h>
#include <whb/log.h>
#include <whb/log_udp.h>

void initLogging() {
    WHBLogUdpInit();
}

void deinitLogging() {
    WHBLogUdpDeinit();
}

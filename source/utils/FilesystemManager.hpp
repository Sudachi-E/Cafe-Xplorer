#pragma once
#include <string>

class FilesystemManager {
public:
    static void Initialize();
    static void MountAllFilesystems();
    static bool MountFatUsb();
    static void UnmountFatUsb();
    static bool IsFatUsbMounted();
    static bool PollDrives();
    static void UnmountAllFilesystems();
    static void Shutdown();
    static bool IsMochaInitialized();
private:
    static bool sMochaInitialized;
    static int sFatUsbDriveIndex;
    static bool sWfsMounted[2];
    static uint64_t sLastPollTick;
};

#pragma once

class FilesystemManager {
public:
    static void Initialize();
    static void MountAllFilesystems();
    static bool MountFatUsb();
    static void UnmountFatUsb();
    static bool IsFatUsbMounted();
    static void UnmountAllFilesystems();
    static void Shutdown();
    static bool IsMochaInitialized();
private:
    static bool sMochaInitialized;
    static int sFatUsbDriveIndex;
};

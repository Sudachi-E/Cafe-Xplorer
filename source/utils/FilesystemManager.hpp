#pragma once

class FilesystemManager {
public:
    static void Initialize();
    static void MountAllFilesystems();
    static void UnmountAllFilesystems();
    static void Shutdown();
    static bool IsMochaInitialized();

    static bool MountFatUsb();
    static bool MountSdCardVolume();
    static void UnmountFatUsb();
    static bool IsFatUsbMounted();

private:
    static bool sMochaInitialized;
};

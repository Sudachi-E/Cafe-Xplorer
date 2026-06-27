#pragma once

class FatUsbManager {
public:
    static bool MountUSBDrive(int driveIndex = 1);
    static bool MountSdCard();
    static void UnmountUSBDrive(int driveIndex = 1);
    static bool IsMounted(int driveIndex = 1);
    static bool IsDriveAlive(int driveIndex);
    static bool ProbeDrive(int driveIndex);
    static void Shutdown();
private:
    static bool MountDrive(int driveIndex, const char* devName);
    static bool sInitialized;
    static bool sDriveMounted[3];
    static void* sFatFs[3];
};

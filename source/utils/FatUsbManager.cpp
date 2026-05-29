#include "FatUsbManager.hpp"
#include "FatFsDevoptab.hpp"
#include "FilesystemManager.hpp"
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include "../filemanager/PathConverter.hpp"
#include <whb/log.h>
#include <mocha/mocha.h>
#include <cstring>
#include <cstdio>

bool FatUsbManager::sInitialized = false;
bool FatUsbManager::sDriveMounted[3] = {false, false, false};
void* FatUsbManager::sFatFs[3] = {nullptr, nullptr, nullptr};

bool FatUsbManager::MountUSBDrive(int driveIndex) {
    return MountDrive(driveIndex, "fat_usb");
}

bool FatUsbManager::MountSdCard() {
    return MountDrive(0, "fat_sd");
}

bool FatUsbManager::MountDrive(int driveIndex, const char* devName) {
    if (sDriveMounted[driveIndex]) {
        PathConverter::AddRootDirectory(devName);
        return true;
    }

    if (!FilesystemManager::IsMochaInitialized()) {
        WHBLogPrintf("FAT32 mount skipped for drive %d - Mocha not initialized", driveIndex);
        return false;
    }

    WHBLogPrintf("Mounting FAT32 drive %d as %s...", driveIndex, devName);

    DSTATUS ds = disk_initialize((BYTE)driveIndex);
    if (ds != 0) {
        WHBLogPrintf("disk_initialize failed for drive %d: %d", driveIndex, ds);
        return false;
    }

    FATFS* fatfs = (FATFS*)aligned_alloc(0x40, sizeof(FATFS));
    if (!fatfs) {
        WHBLogPrintf("Failed to allocate FATFS object");
        disk_shutdown((BYTE)driveIndex);
        return false;
    }
    memset(fatfs, 0, sizeof(FATFS));

    // Apply sector offset for non-standard partition layouts (USB boot sector at LBA 2048)
    if (driveIndex == 1) {
        g_sectorOffset[driveIndex] = 2048;
        WHBLogPrintf("FAT32: using sector offset 2048 for drive %d", driveIndex);
    }

    char mountPath[16];
    snprintf(mountPath, sizeof(mountPath), "%d:", driveIndex);

    FRESULT fr = f_mount(fatfs, mountPath, 1);
    if (fr != FR_OK) {
        WHBLogPrintf("f_mount failed for drive %d: %d", driveIndex, fr);
        free(fatfs);
        disk_shutdown((BYTE)driveIndex);
        return false;
    }

    char drivePath[8];
    snprintf(drivePath, sizeof(drivePath), "%d:", driveIndex);
    f_chdrive(drivePath);

    if (!FatFsDevoptab_Register(devName, (BYTE)driveIndex)) {
        WHBLogPrintf("Failed to register devoptab for %s", devName);
        f_unmount(mountPath);
        free(fatfs);
        disk_shutdown((BYTE)driveIndex);
        return false;
    }

    PathConverter::AddRootDirectory(devName);

    sFatFs[driveIndex] = fatfs;
    sDriveMounted[driveIndex] = true;

    WHBLogPrintf("FAT32 drive %d (%s) mounted successfully", driveIndex, devName);
    return true;
}

void FatUsbManager::UnmountUSBDrive(int driveIndex) {
    if (!sDriveMounted[driveIndex]) {
        WHBLogPrintf("FAT32 drive %d not mounted, skipping unmount", driveIndex);
        return;
    }

    WHBLogPrintf("[umount] Unmounting FAT32 drive %d...", driveIndex);

    WHBLogPrintf("[umount] devoptab unregister...");
    FatFsDevoptab_Unregister();

    char mountPath[16];
    snprintf(mountPath, sizeof(mountPath), "%d:", driveIndex);
    WHBLogPrintf("[umount] f_unmount(%s)...", mountPath);
    FRESULT fr = f_unmount(mountPath);
    if (fr != FR_OK) {
        WHBLogPrintf("[umount] f_unmount failed: %d", fr);
    }

    if (sFatFs[driveIndex]) {
        free(sFatFs[driveIndex]);
        sFatFs[driveIndex] = nullptr;
    }

    WHBLogPrintf("[umount] disk_shutdown(%d)...", driveIndex);
    disk_shutdown((BYTE)driveIndex);

    sDriveMounted[driveIndex] = false;
    WHBLogPrintf("[umount] FAT32 drive %d unmounted successfully", driveIndex);
}

bool FatUsbManager::IsMounted(int driveIndex) {
    return sDriveMounted[driveIndex];
}

void FatUsbManager::Shutdown() {
    for (int i = 0; i < 3; i++) {
        if (sDriveMounted[i]) {
            UnmountUSBDrive(i);
        }
    }
}

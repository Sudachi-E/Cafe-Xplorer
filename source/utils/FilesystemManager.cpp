#include "FilesystemManager.hpp"
#include "FatUsbManager.hpp"
#include "../filemanager/PathConverter.hpp"
#include <mocha/mocha.h>
#include <whb/log.h>
#include <dirent.h>

bool FilesystemManager::sMochaInitialized = false;

void FilesystemManager::Initialize() {
    if (!sMochaInitialized) {
        MochaUtilsStatus status = Mocha_InitLibrary();
        if (status == MOCHA_RESULT_SUCCESS) {
            sMochaInitialized = true;
            WHBLogPrintf("Mocha initialized successfully");
        } else {
            WHBLogPrintf("Failed to initialize Mocha: %d", status);
        }
    }
}

void FilesystemManager::MountAllFilesystems() {
    if (!sMochaInitialized) {
        Initialize();
    }
    
    if (!sMochaInitialized) {
        WHBLogPrintf("Cannot mount filesystems - Mocha not initialized");
        return;
    }
    
    WHBLogPrintf("Mounting all filesystems...");

    static bool sDevoptabsInitialized = false;

    auto oneTimeMount = [&](const char* name, const char* path) -> MochaUtilsStatus {
        if (!sDevoptabsInitialized)
            return Mocha_MountFS(name, nullptr, path);
        return MOCHA_RESULT_SUCCESS;
    };

    MochaUtilsStatus mountRes;

    mountRes = oneTimeMount("slccmpt01", "/vol/storage_slccmpt01");
    if (mountRes == MOCHA_RESULT_SUCCESS || mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        if (!sDevoptabsInitialized)
            WHBLogPrintf("slccmpt01 mounted successfully");
        PathConverter::AddRootDirectory("slccmpt01");
    }

    mountRes = oneTimeMount("storage_slc", "/vol/storage_slc01");
    if (mountRes == MOCHA_RESULT_SUCCESS || mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        if (!sDevoptabsInitialized)
            WHBLogPrintf("SLC filesystem mounted successfully");
        PathConverter::AddRootDirectory("storage_slc");
    }

    mountRes = oneTimeMount("storage_mlc", "/vol/storage_mlc01");
    if (mountRes == MOCHA_RESULT_SUCCESS || mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        if (!sDevoptabsInitialized)
            WHBLogPrintf("MLC filesystem mounted successfully");
        PathConverter::AddRootDirectory("storage_mlc");
    }
    
    mountRes = oneTimeMount("storage_odd_tickets", "/vol/storage_odd01");
    if (mountRes == MOCHA_RESULT_SUCCESS || mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        if (!sDevoptabsInitialized)
            WHBLogPrintf("storage_odd_tickets mounted successfully");
        PathConverter::AddRootDirectory("storage_odd_tickets");
    }
    
    mountRes = oneTimeMount("storage_odd_updates", "/vol/storage_odd02");
    if (mountRes == MOCHA_RESULT_SUCCESS || mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        if (!sDevoptabsInitialized)
            WHBLogPrintf("storage_odd_updates mounted successfully");
        PathConverter::AddRootDirectory("storage_odd_updates");
    }
    
    mountRes = oneTimeMount("storage_odd_content", "/vol/storage_odd03");
    if (mountRes == MOCHA_RESULT_SUCCESS || mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        if (!sDevoptabsInitialized)
            WHBLogPrintf("storage_odd_content mounted successfully");
        PathConverter::AddRootDirectory("storage_odd_content");
    }
    
    mountRes = oneTimeMount("storage_odd_content2", "/vol/storage_odd04");
    if (mountRes == MOCHA_RESULT_SUCCESS || mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        if (!sDevoptabsInitialized)
            WHBLogPrintf("storage_odd_content2 mounted successfully");
        PathConverter::AddRootDirectory("storage_odd_content2");
    }
    
    mountRes = oneTimeMount("storage_usb", "/vol/storage_usb01");
    if (mountRes == MOCHA_RESULT_SUCCESS || mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        DIR* testDir = opendir("storage_usb:/");
        if (testDir) {
            closedir(testDir);
            WHBLogPrintf("WFS USB detected at /vol/storage_usb01");
            PathConverter::AddRootDirectory("storage_usb");
        } else {
            WHBLogPrintf("No WFS USB at /vol/storage_usb01 — cleaning up");
            if (!sDevoptabsInitialized)
                Mocha_UnmountFS("storage_usb");
        }
    }

    MountFatUsb();

    mountRes = oneTimeMount("storage_sd", "/vol/external01");
    if (mountRes == MOCHA_RESULT_SUCCESS || mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        DIR* testDir = opendir("storage_sd:/");
        if (testDir) {
            closedir(testDir);
            WHBLogPrintf("SD card accessible via storage_sd:/");
            PathConverter::AddRootDirectory("storage_sd");
        } else {
            WHBLogPrintf("SD card /vol/external01 not accessible via FSA — cleaning up");
            if (!sDevoptabsInitialized)
                Mocha_UnmountFS("storage_sd");
        }
    }

    sDevoptabsInitialized = true;
}

bool FilesystemManager::MountFatUsb() {
    WHBLogPrintf("Attempting to mount FAT32 USB drive...");
    return FatUsbManager::MountUSBDrive(1);
}

void FilesystemManager::UnmountFatUsb() {
    WHBLogPrintf("Unmounting FAT32 USB drive...");
    FatUsbManager::UnmountUSBDrive(1);
}

bool FilesystemManager::IsFatUsbMounted() {
    return FatUsbManager::IsMounted(1);
}

void FilesystemManager::UnmountAllFilesystems() {
    if (!sMochaInitialized) {
        return;
    }
    
    WHBLogPrintf("[unmount] Unmounting all filesystems...");

    WHBLogPrintf("[unmount] Raw FAT32 USB...");
    UnmountFatUsb();
    
    WHBLogPrintf("[unmount] Mocha volumes...");
    Mocha_UnmountFS("storage_odd_tickets"); WHBLogPrintf("[unmount] storage_odd_tickets done");
    Mocha_UnmountFS("storage_odd_updates"); WHBLogPrintf("[unmount] storage_odd_updates done");
    Mocha_UnmountFS("storage_odd_content"); WHBLogPrintf("[unmount] storage_odd_content done");
    Mocha_UnmountFS("storage_odd_content2"); WHBLogPrintf("[unmount] storage_odd_content2 done");
    Mocha_UnmountFS("storage_usb"); WHBLogPrintf("[unmount] storage_usb done");
    Mocha_UnmountFS("storage_sd"); WHBLogPrintf("[unmount] storage_sd done");
    Mocha_UnmountFS("slccmpt01"); WHBLogPrintf("[unmount] slccmpt01 done");
    Mocha_UnmountFS("storage_slc"); WHBLogPrintf("[unmount] storage_slc done");
    Mocha_UnmountFS("storage_mlc"); WHBLogPrintf("[unmount] storage_mlc done");
    
    WHBLogPrintf("[unmount] All filesystems unmounted");
}

void FilesystemManager::Shutdown() {
    if (!sMochaInitialized) {
        return;
    }
    
    WHBLogPrintf("Deinitializing Mocha...");
    Mocha_DeInitLibrary();
    sMochaInitialized = false;
    WHBLogPrintf("Mocha deinitialized");
}

bool FilesystemManager::IsMochaInitialized() {
    return sMochaInitialized;
}

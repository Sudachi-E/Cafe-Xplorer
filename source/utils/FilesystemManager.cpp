#include "FilesystemManager.hpp"
#include "FatUsbManager.hpp"
#include "../filemanager/PathConverter.hpp"
#include <mocha/mocha.h>
#include <whb/log.h>
#include <dirent.h>

bool FilesystemManager::sMochaInitialized = false;
int FilesystemManager::sFatUsbDriveIndex = -1;

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

    const char* wfsPaths[] = {"/vol/storage_usb01", "/vol/storage_usb02"};
    for (int i = 0; i < 2; i++) {
        char name[32];
        snprintf(name, sizeof(name), "storage_wfs", i + 1);
        mountRes = oneTimeMount(name, wfsPaths[i]);
        if (mountRes == MOCHA_RESULT_SUCCESS || mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
            std::string dirPath = std::string(name) + ":/";
            DIR* testDir = opendir(dirPath.c_str());
            if (testDir) {
                closedir(testDir);
                WHBLogPrintf("WFS USB detected at %s", wfsPaths[i]);
                PathConverter::AddRootDirectory(name);
            } else {
                WHBLogPrintf("No WFS USB at %s — cleaning up", wfsPaths[i]);
                if (!sDevoptabsInitialized)
                    Mocha_UnmountFS(name);
            }
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
    if (FatUsbManager::MountUSBDrive(1)) {
        sFatUsbDriveIndex = 1;
        return true;
    }
    if (FatUsbManager::MountUSBDrive(2)) {
        sFatUsbDriveIndex = 2;
        return true;
    }
    sFatUsbDriveIndex = -1;
    return false;
}

void FilesystemManager::UnmountFatUsb() {
    if (sFatUsbDriveIndex < 0) {
        WHBLogPrintf("No FAT32 drive to unmount");
        return;
    }
    WHBLogPrintf("Unmounting FAT32 USB drive %d...", sFatUsbDriveIndex);
    FatUsbManager::UnmountUSBDrive(sFatUsbDriveIndex);
    sFatUsbDriveIndex = -1;
}

bool FilesystemManager::IsFatUsbMounted() {
    return sFatUsbDriveIndex >= 0 && FatUsbManager::IsMounted(sFatUsbDriveIndex);
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
    Mocha_UnmountFS("storage_usb01"); WHBLogPrintf("[unmount] storage_usb01 done");
    Mocha_UnmountFS("storage_usb02"); WHBLogPrintf("[unmount] storage_usb02 done");
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

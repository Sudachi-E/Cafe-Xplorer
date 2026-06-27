#include "FilesystemManager.hpp"
#include "FatUsbManager.hpp"
#include "../filemanager/PathConverter.hpp"
#include <mocha/mocha.h>
#include <whb/log.h>
#include <dirent.h>
#include <coreinit/time.h>

bool FilesystemManager::sMochaInitialized = false;
int FilesystemManager::sFatUsbDriveIndex = -1;
bool FilesystemManager::sWfsMounted[2] = {false, false};
uint64_t FilesystemManager::sLastPollTick = 0;

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

    // Mount WFS volumes first (must happen before raw I/O which corrupts FSA)
    {
        const char* wfsPaths[] = {"/vol/storage_usb01", "/vol/storage_usb02"};
        for (int i = 0; i < 2; i++) {
            char name[32];
            snprintf(name, sizeof(name), "storage_usb%02d", i + 1);
            mountRes = oneTimeMount(name, wfsPaths[i]);
            if (mountRes == MOCHA_RESULT_SUCCESS || mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
                std::string dirPath = std::string(name) + ":/";
                DIR* testDir = opendir(dirPath.c_str());
                if (testDir) {
                    closedir(testDir);
                    WHBLogPrintf("WFS USB detected at %s as %s", wfsPaths[i], name);
                    PathConverter::AddRootDirectory(name);
                    sWfsMounted[i] = true;
                } else {
                    WHBLogPrintf("No WFS USB at %s — cleaning up", wfsPaths[i]);
                    if (!sDevoptabsInitialized)
                        Mocha_UnmountFS(name);
                }
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

bool FilesystemManager::PollDrives() {
    uint64_t now = OSTicksToMilliseconds(OSGetSystemTime());
    bool anyDriveFound = sFatUsbDriveIndex >= 0 || sWfsMounted[0] || sWfsMounted[1];
    uint64_t interval = anyDriveFound ? 3000 : 60000;
    if (now - sLastPollTick < interval)
        return false;
    sLastPollTick = now;

    bool changed = false;

    // Checks mounted FAT32 drive for removal
    if (sFatUsbDriveIndex >= 0 && !FatUsbManager::ProbeDrive(sFatUsbDriveIndex)) {
        WHBLogPrintf("[hotplug] FAT32 drive %d removed", sFatUsbDriveIndex);
        FatUsbManager::UnmountUSBDrive(sFatUsbDriveIndex);
        PathConverter::RemoveRootDirectory("fat_usb");
        sFatUsbDriveIndex = -1;
        changed = true;
    }

    // Detect new FAT32 drive
    if (sFatUsbDriveIndex < 0) {
        if (FatUsbManager::ProbeDrive(1)) {
            WHBLogPrintf("[hotplug] FAT32 BPB detected on slot 1, mounting...");
            if (FatUsbManager::MountUSBDrive(1)) {
                sFatUsbDriveIndex = 1;
                changed = true;
            }
        } else if (FatUsbManager::ProbeDrive(2)) {
            WHBLogPrintf("[hotplug] FAT32 BPB detected on slot 2, mounting...");
            if (FatUsbManager::MountUSBDrive(2)) {
                sFatUsbDriveIndex = 2;
                changed = true;
            }
        }
    }

    // Check mounted WFS drives for removal
    const char* wfsNames[] = {"storage_usb01", "storage_usb02"};
    for (int i = 0; i < 2; i++) {
        if (!sWfsMounted[i]) continue;
        std::string testPath = std::string(wfsNames[i]) + ":/";
        DIR* d = opendir(testPath.c_str());
        if (d) {
            closedir(d);
        } else {
            WHBLogPrintf("[hotplug] WFS %s removed", wfsNames[i]);
            Mocha_UnmountFS(wfsNames[i]);
            PathConverter::RemoveRootDirectory(wfsNames[i]);
            sWfsMounted[i] = false;
            changed = true;
        }
    }

    const char* wfsPaths[] = {"/vol/storage_usb01", "/vol/storage_usb02"};
    for (int i = 0; i < 2; i++) {
        if (sWfsMounted[i]) continue;
        int usbSlot = i + 1;
        if (FatUsbManager::IsMounted(usbSlot)) continue;

        MochaUtilsStatus mres = Mocha_MountFS(wfsNames[i], nullptr, wfsPaths[i]);
        if (mres == MOCHA_RESULT_SUCCESS || mres == MOCHA_RESULT_ALREADY_EXISTS) {
            std::string testPath = std::string(wfsNames[i]) + ":/";
            DIR* d = opendir(testPath.c_str());
            if (d) {
                closedir(d);
                WHBLogPrintf("[hotplug] New WFS drive detected at %s", wfsPaths[i]);
                PathConverter::AddRootDirectory(wfsNames[i]);
                sWfsMounted[i] = true;
                changed = true;
            } else {
                Mocha_UnmountFS(wfsNames[i]);
            }
        }
    }

    if (changed) {
        WHBLogPrintf("[hotplug] Drive state changed");
    }

    return changed;
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
    
    for (int i = 0; i < 2; i++)
        sWfsMounted[i] = false;
    
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

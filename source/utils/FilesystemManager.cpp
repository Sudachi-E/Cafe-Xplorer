#include "FilesystemManager.hpp"
#include "../filemanager/PathConverter.hpp"
#include <mocha/mocha.h>
#include <whb/log.h>

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
    
    auto mountRes = Mocha_MountFS("slccmpt01", "/dev/slccmpt01", "/vol/storage_slccmpt01");
    if (mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        mountRes = Mocha_MountFS("slccmpt01", nullptr, "/vol/storage_slccmpt01");
    }
    if (mountRes == MOCHA_RESULT_SUCCESS) {
        WHBLogPrintf("slccmpt01 mounted successfully");
        PathConverter::AddRootDirectory("slccmpt01");
    }
    
    mountRes = Mocha_MountFS("storage_odd_tickets", nullptr, "/vol/storage_odd01");
    if (mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        mountRes = Mocha_MountFS("storage_odd_tickets", nullptr, "/vol/storage_odd01");
    }
    if (mountRes == MOCHA_RESULT_SUCCESS) {
        WHBLogPrintf("storage_odd_tickets mounted successfully");
        PathConverter::AddRootDirectory("storage_odd_tickets");
    }
    
    mountRes = Mocha_MountFS("storage_odd_updates", nullptr, "/vol/storage_odd02");
    if (mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        mountRes = Mocha_MountFS("storage_odd_updates", nullptr, "/vol/storage_odd02");
    }
    if (mountRes == MOCHA_RESULT_SUCCESS) {
        WHBLogPrintf("storage_odd_updates mounted successfully");
        PathConverter::AddRootDirectory("storage_odd_updates");
    }
    
    mountRes = Mocha_MountFS("storage_odd_content", nullptr, "/vol/storage_odd03");
    if (mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        mountRes = Mocha_MountFS("storage_odd_content", nullptr, "/vol/storage_odd03");
    }
    if (mountRes == MOCHA_RESULT_SUCCESS) {
        WHBLogPrintf("storage_odd_content mounted successfully");
        PathConverter::AddRootDirectory("storage_odd_content");
    }
    
    mountRes = Mocha_MountFS("storage_odd_content2", nullptr, "/vol/storage_odd04");
    if (mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        mountRes = Mocha_MountFS("storage_odd_content2", nullptr, "/vol/storage_odd04");
    }
    if (mountRes == MOCHA_RESULT_SUCCESS) {
        WHBLogPrintf("storage_odd_content2 mounted successfully");
        PathConverter::AddRootDirectory("storage_odd_content2");
    }
    
    mountRes = Mocha_MountFS("storage_slc", "/dev/slc01", "/vol/storage_slc01");
    if (mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        mountRes = Mocha_MountFS("storage_slc", nullptr, "/vol/storage_slc01");
    }
    if (mountRes == MOCHA_RESULT_SUCCESS) {
        WHBLogPrintf("SLC filesystem mounted successfully");
        PathConverter::AddRootDirectory("storage_slc");
    }
    
    mountRes = Mocha_MountFS("storage_mlc", nullptr, "/vol/storage_mlc01");
    if (mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        mountRes = Mocha_MountFS("storage_mlc", nullptr, "/vol/storage_mlc01");
    }
    if (mountRes == MOCHA_RESULT_SUCCESS) {
        WHBLogPrintf("MLC filesystem mounted successfully");
        PathConverter::AddRootDirectory("storage_mlc");
    }
    
    mountRes = Mocha_MountFS("storage_usb", nullptr, "/vol/storage_usb01");
    if (mountRes == MOCHA_RESULT_ALREADY_EXISTS) {
        mountRes = Mocha_MountFS("storage_usb", nullptr, "/vol/storage_usb01");
    }
    if (mountRes == MOCHA_RESULT_SUCCESS) {
        WHBLogPrintf("USB filesystem mounted successfully");
        PathConverter::AddRootDirectory("storage_usb");
    }
}

void FilesystemManager::UnmountAllFilesystems() {
    if (!sMochaInitialized) {
        return;
    }
    
    WHBLogPrintf("Unmounting all filesystems...");
    
    Mocha_UnmountFS("slccmpt01");
    Mocha_UnmountFS("storage_odd_tickets");
    Mocha_UnmountFS("storage_odd_updates");
    Mocha_UnmountFS("storage_odd_content");
    Mocha_UnmountFS("storage_odd_content2");
    Mocha_UnmountFS("storage_slc");
    Mocha_UnmountFS("storage_mlc");
    Mocha_UnmountFS("storage_usb");
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

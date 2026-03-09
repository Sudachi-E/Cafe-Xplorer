#pragma once

class FilesystemManager {
public:
    static void Initialize();
    static void MountAllFilesystems();
    static void UnmountAllFilesystems();
    static bool IsMochaInitialized();
    
private:
    static bool sMochaInitialized;
};

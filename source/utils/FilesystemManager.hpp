#pragma once

class FilesystemManager {
public:
    static void Initialize();
    static void MountAllFilesystems();
    static void UnmountAllFilesystems();
    static void Shutdown();
    static bool IsMochaInitialized();
    
private:
    static bool sMochaInitialized;
};

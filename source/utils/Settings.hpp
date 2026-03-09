#pragma once
#include <string>

class Settings {
public:
    static void Initialize();
    static void Load();
    static void Save();
    
    static bool GetFullFilesystemAccess();
    static void SetFullFilesystemAccess(bool enabled);
    
private:
    static bool sFullFilesystemAccess;
    static bool sInitialized;
    static std::string GetSettingsPath();
};

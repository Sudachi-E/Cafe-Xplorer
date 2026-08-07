#pragma once
#include <string>

class Settings {
public:
    static void Initialize();
    static void Load();
    static void Save();
    
    static bool GetFullFilesystemAccess();
    static void SetFullFilesystemAccess(bool enabled);
    static bool GetFtpServerEnabled();
    static void SetFtpServerEnabled(bool enabled);
    static bool GetShowHiddenFiles();
    static void SetShowHiddenFiles(bool enabled);
    
private:
    static bool sFullFilesystemAccess;
    static bool sFtpServerEnabled;
    static bool sShowHiddenFiles;
    static bool sInitialized;
    static std::string GetSettingsPath();
    static std::string GetSavePath();
};

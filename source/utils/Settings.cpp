#include "Settings.hpp"
#include <fstream>
#include <whb/log.h>
#include <sys/stat.h>
#include <errno.h>

bool Settings::sFullFilesystemAccess = false;
bool Settings::sInitialized = false;

std::string Settings::GetSettingsPath() {
    return "fs:/vol/external01/wiiu/apps/Cafe-Xplorer/settings.cfg";
}

void Settings::Initialize() {
    if (sInitialized) return;
    Load();
    mkdir("fs:/vol/external01/wiiu", 0777);
    mkdir("fs:/vol/external01/wiiu/apps", 0777);
    mkdir("fs:/vol/external01/wiiu/apps/Cafe-Xplorer", 0777);
    sInitialized = true;
}

void Settings::Load() {
    std::ifstream file(GetSettingsPath());
    if (!file.is_open()) {
        WHBLogPrintf("Settings file not found, using defaults");
        sFullFilesystemAccess = false;
        return;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("full_filesystem_access=") == 0) {
            std::string value = line.substr(23);
            sFullFilesystemAccess = (value == "1" || value == "true");
            WHBLogPrintf("Loaded setting: full_filesystem_access = %d", sFullFilesystemAccess);
        }
    }
    
    file.close();
}

void Settings::Save() {
    std::string settingsPath = GetSettingsPath();
    std::ofstream file(settingsPath);
    if (!file.is_open()) {
        WHBLogPrintf("Failed to save settings file to: %s (dir missing?)", settingsPath.c_str());
        WHBLogPrintf("  setting still applied in memory, may not persist across reboot");
        return;
    }
    
    file << "full_filesystem_access=" << (sFullFilesystemAccess ? "1" : "0") << std::endl;
    file.close();
    
    WHBLogPrintf("Settings saved successfully to: %s", settingsPath.c_str());
    WHBLogPrintf("  full_filesystem_access = %d", sFullFilesystemAccess);
}

bool Settings::GetFullFilesystemAccess() {
    if (!sInitialized) Initialize();
    return sFullFilesystemAccess;
}

void Settings::SetFullFilesystemAccess(bool enabled) {
    sFullFilesystemAccess = enabled;
}

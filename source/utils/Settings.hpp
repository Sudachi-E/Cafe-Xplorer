#pragma once
#include <string>

enum class DateFormat {
    DayMonthYear,
    MonthDayYear,
};

enum class KeyboardType {
    System,
    Custom,
};

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
    static DateFormat GetDateFormat();
    static void SetDateFormat(DateFormat format);
    static KeyboardType GetKeyboardType();
    static void SetKeyboardType(KeyboardType type);
    
private:
    static bool sFullFilesystemAccess;
    static bool sFtpServerEnabled;
    static bool sShowHiddenFiles;
    static DateFormat sDateFormat;
    static KeyboardType sKeyboardType;
    static bool sInitialized;
    static std::string GetSettingsPath();
    static std::string GetSavePath();
};

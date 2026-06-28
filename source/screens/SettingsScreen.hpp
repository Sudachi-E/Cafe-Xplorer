#pragma once
#include "Screen.hpp"
#include "../input/Input.h"
#include <string>

class SettingsScreen : public Screen {
public:
    SettingsScreen();
    ~SettingsScreen() override = default;
    
    void Draw() override;
    bool Update(Input& input) override;
    
    bool ShouldClose() const { return mShouldClose; }
    bool SettingsChanged() const { return mSettingsChanged; }
    
private:
    int mSelectedOption;
    bool mShouldClose;
    bool mSettingsChanged;
    bool mFullFilesystemAccess;
    bool mFtpServerEnabled;
    bool mShowFtpResult;
    int mFtpModalOption;
    std::string mFtpResultIP;
    
    void ToggleFullFilesystemAccess();
    void ToggleFtpServer();
    void SaveSettings();
    void LoadSettings();
};

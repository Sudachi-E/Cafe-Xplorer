#include "SettingsScreen.hpp"
#include "../Gfx.hpp"
#include "../utils/Settings.hpp"
#include <whb/log.h>

SettingsScreen::SettingsScreen() 
    : mSelectedOption(0)
    , mShouldClose(false)
    , mSettingsChanged(false)
    , mFullFilesystemAccess(false)
{
    LoadSettings();
}

void SettingsScreen::LoadSettings() {
    mFullFilesystemAccess = Settings::GetFullFilesystemAccess();
}

void SettingsScreen::SaveSettings() {
    Settings::SetFullFilesystemAccess(mFullFilesystemAccess);
    Settings::Save();
    mSettingsChanged = true;
    WHBLogPrintf("Settings saved: Full filesystem access = %d", mFullFilesystemAccess);
}

void SettingsScreen::ToggleFullFilesystemAccess() {
    mFullFilesystemAccess = !mFullFilesystemAccess;
    WHBLogPrintf("Toggled full filesystem access: %d", mFullFilesystemAccess);
    SaveSettings();
}

void SettingsScreen::Draw() {
    Gfx::Clear(Gfx::COLOR_BACKGROUND);
    
    DrawTopBar("Settings");
    
    int yPos = 150;
    int lineHeight = 50;
    
    Gfx::Print(640, 100, 32, Gfx::COLOR_TEXT, "Settings", Gfx::ALIGN_CENTER);
    
    SDL_Color optionColor = (mSelectedOption == 0) ? Gfx::COLOR_HIGHLIGHTED : Gfx::COLOR_TEXT;
    std::string checkboxText = mFullFilesystemAccess ? "[X]" : "[ ]";
    std::string optionText = checkboxText + " Full Wii U Filesystem Access";
    Gfx::Print(200, yPos, 28, optionColor, optionText, Gfx::ALIGN_LEFT);
    
    if (mSelectedOption == 0) {
        Gfx::Print(200, yPos + 30, 20, Gfx::COLOR_TEXT, 
            "Access all system storage", Gfx::ALIGN_LEFT);
    }
    
    DrawBottomBar("A: Toggle  B: Back", nullptr, nullptr);
}

bool SettingsScreen::Update(Input& input) {
    if (input.data.buttons_d & Input::BUTTON_B) {
        mShouldClose = true;
        return true;
    }
    
    if (input.data.buttons_d & Input::BUTTON_A) {
        if (mSelectedOption == 0) {
            ToggleFullFilesystemAccess();
        }
    }
    
    return true;
}

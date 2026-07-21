#include "SettingsScreen.hpp"
#include "../Gfx.hpp"
#include "../utils/Settings.hpp"
#include "../utils/FtpServer.hpp"
#include <whb/log.h>

SettingsScreen::SettingsScreen()
    : mSelectedOption(0)
    , mShouldClose(false)
    , mSettingsChanged(false)
    , mFullFilesystemAccess(false)
    , mFtpServerEnabled(false)
    , mShowFtpResult(false)
    , mFtpModalOption(0)
{
    LoadSettings();
}

void SettingsScreen::LoadSettings() {
    mFullFilesystemAccess = Settings::GetFullFilesystemAccess();
    mFtpServerEnabled = Settings::GetFtpServerEnabled();
}

void SettingsScreen::SaveSettings() {
    Settings::SetFullFilesystemAccess(mFullFilesystemAccess);
    Settings::SetFtpServerEnabled(mFtpServerEnabled);
    Settings::Save();
    mSettingsChanged = true;
    WHBLogPrintf("Settings saved: full_filesystem_access=%d ftp_server_enabled=%d",
                 mFullFilesystemAccess, mFtpServerEnabled);
}

void SettingsScreen::ToggleFullFilesystemAccess() {
    mFullFilesystemAccess = !mFullFilesystemAccess;
    WHBLogPrintf("Toggled full filesystem access: %d", mFullFilesystemAccess);
    SaveSettings();
}

void SettingsScreen::ToggleFtpServer() {
    if (mFtpServerEnabled) {
        FtpServer::Stop();
        mFtpServerEnabled = false;
        mShowFtpResult = false;
        WHBLogPrintf("FTP server stopped");
        SaveSettings();
    } else {
        if (FtpServer::Start()) {
            mFtpServerEnabled = true;
            mShowFtpResult = true;
            mFtpModalOption = 0;
            mFtpResultIP = FtpServer::GetLocalIP();
            WHBLogPrintf("FTP server started on %s:%d", mFtpResultIP.c_str(), FtpServer::GetPort());
            SaveSettings();
        } else {
            WHBLogPrintf("FTP server failed to start");
        }
    }
}

void SettingsScreen::Draw() {
    Gfx::Clear(Gfx::COLOR_BACKGROUND);

    if (mShowFtpResult) {
        int modalWidth = 800;
        int modalHeight = 360;
        int modalX = (Gfx::SCREEN_WIDTH - modalWidth) / 2;
        int modalY = (Gfx::SCREEN_HEIGHT - modalHeight) / 2;
        int borderWidth = 4;

        Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, SDL_Color{0, 0, 0, 180});
        Gfx::DrawRectFilled(modalX, modalY, modalWidth, modalHeight, Gfx::COLOR_ALT_BACKGROUND);
        Gfx::DrawRectFilled(modalX, modalY, modalWidth, borderWidth, Gfx::COLOR_HIGHLIGHTED);
        Gfx::DrawRectFilled(modalX, modalY + modalHeight - borderWidth, modalWidth, borderWidth, Gfx::COLOR_HIGHLIGHTED);
        Gfx::DrawRectFilled(modalX, modalY, borderWidth, modalHeight, Gfx::COLOR_HIGHLIGHTED);
        Gfx::DrawRectFilled(modalX + modalWidth - borderWidth, modalY, borderWidth, modalHeight, Gfx::COLOR_HIGHLIGHTED);

        Gfx::Print(modalX + modalWidth / 2, modalY + 50, 42, Gfx::COLOR_WHITE,
                   "FTP Server Running", Gfx::ALIGN_CENTER);

        std::string ipText = "IP: " + mFtpResultIP + " : Port: " + std::to_string(FtpServer::GetPort());
        Gfx::Print(modalX + modalWidth / 2, modalY + 120, 48, Gfx::COLOR_WHITE,
                   ipText, Gfx::ALIGN_CENTER);

        int buttonY = modalY + 210;
        int buttonWidth = 280;
        int buttonHeight = 60;
        int buttonSpacing = 40;
        int bgX = modalX + (modalWidth / 2) - buttonWidth - (buttonSpacing / 2);
        int stopX = modalX + (modalWidth / 2) + (buttonSpacing / 2);

        SDL_Color bgColor = (mFtpModalOption == 0) ? Gfx::COLOR_HIGHLIGHTED : Gfx::COLOR_BARS;
        SDL_Color stopColor = (mFtpModalOption == 1) ? Gfx::COLOR_HIGHLIGHTED : Gfx::COLOR_BARS;

        Gfx::DrawRectFilled(bgX, buttonY, buttonWidth, buttonHeight, bgColor);
        Gfx::Print(bgX + buttonWidth / 2, buttonY + buttonHeight / 2 + 5, 36,
                   Gfx::COLOR_WHITE, "Background (A)", Gfx::ALIGN_CENTER);

        Gfx::DrawRectFilled(stopX, buttonY, buttonWidth, buttonHeight, stopColor);
        Gfx::Print(stopX + buttonWidth / 2, buttonY + buttonHeight / 2 + 5, 36,
                   Gfx::COLOR_WHITE, "Stop (B)", Gfx::ALIGN_CENTER);

        Gfx::Print(modalX + modalWidth / 2, modalY + modalHeight - 30, 24, Gfx::COLOR_WHITE,
                   "A: Confirm  B: Cancel", Gfx::ALIGN_CENTER);
        return;
    }

    DrawTopBar("Settings");
    Gfx::Print(Gfx::SCREEN_WIDTH - 40, 40, 32, Gfx::COLOR_TEXT, "v1.8", Gfx::ALIGN_RIGHT | Gfx::ALIGN_VERTICAL);
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, 40, 48, Gfx::COLOR_TEXT, "Cafe-Xplorer", Gfx::ALIGN_CENTER);

    int yPos = 150;
    Gfx::Print(640, 100, 32, Gfx::COLOR_TEXT, "Settings", Gfx::ALIGN_CENTER);

    SDL_Color opt0Color = (mSelectedOption == 0) ? Gfx::COLOR_HIGHLIGHTED : Gfx::COLOR_TEXT;
    std::string cb0 = mFullFilesystemAccess ? "[X]" : "[ ]";
    Gfx::Print(200, yPos, 28, opt0Color, cb0 + " Full Wii U Filesystem Access", Gfx::ALIGN_LEFT);
    if (mSelectedOption == 0) {
        Gfx::Print(200, yPos + 30, 20, Gfx::COLOR_TEXT,
            "Access all system storage", Gfx::ALIGN_LEFT);
    }

    yPos += 80;
    SDL_Color opt1Color = (mSelectedOption == 1) ? Gfx::COLOR_HIGHLIGHTED : Gfx::COLOR_TEXT;
    std::string cb1 = mFtpServerEnabled ? "[X]" : "[ ]";
    int ftpStatusY = yPos;
    Gfx::Print(200, ftpStatusY, 28, opt1Color, cb1 + " FTP Server", Gfx::ALIGN_LEFT);
    if (FtpServer::IsRunning()) {
        Gfx::Print(200 + Gfx::GetTextWidth(28, cb1 + " FTP Server") + 20, ftpStatusY, 22,
                   Gfx::COLOR_ACCENT, "Running...", Gfx::ALIGN_LEFT);
    }
    if (mSelectedOption == 1) {
        bool running = FtpServer::IsRunning();
        Gfx::Print(200, ftpStatusY + 30, 20, Gfx::COLOR_TEXT,
            running ? "FTP active — toggle OFF to stop" : "Start FTP server on port 2121",
            Gfx::ALIGN_LEFT);
    }

    DrawBottomBar("A: Toggle  B: Back", nullptr, nullptr);
}

bool SettingsScreen::Update(Input& input) {
    if (mShowFtpResult) {
        if (input.data.buttons_d & Input::BUTTON_LEFT) {
            mFtpModalOption = 0;
        }
        if (input.data.buttons_d & Input::BUTTON_RIGHT) {
            mFtpModalOption = 1;
        }
        if (input.data.buttons_d & Input::BUTTON_A) {
            if (mFtpModalOption == 0) {
                mShowFtpResult = false;
                mShouldClose = true;
            } else {
                FtpServer::Stop();
                mFtpServerEnabled = false;
                mShowFtpResult = false;
                SaveSettings();
                mShouldClose = true;
            }
        }
        if (input.data.buttons_d & Input::BUTTON_B) {
            mShowFtpResult = false;
            mShouldClose = true;
        }
        return true;
    }

    if (input.data.buttons_d & Input::BUTTON_B) {
        mShouldClose = true;
        return true;
    }

    if (input.data.buttons_d & Input::BUTTON_DOWN) {
        mSelectedOption = (mSelectedOption + 1) % 2;
    }
    if (input.data.buttons_d & Input::BUTTON_UP) {
        mSelectedOption = (mSelectedOption - 1 + 2) % 2;
    }

    if (input.data.buttons_d & Input::BUTTON_A) {
        if (mSelectedOption == 0) {
            ToggleFullFilesystemAccess();
        } else if (mSelectedOption == 1) {
            ToggleFtpServer();
        }
    }

    return true;
}

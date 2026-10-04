#include "SettingsScreen.hpp"
#include "../Gfx.hpp"
#include "../utils/Settings.hpp"
#include "../utils/FtpServer.hpp"
#include <whb/log.h>

static constexpr int CARD_W       = 860;
static constexpr int CARD_H       = 610;
static constexpr int CARD_RADIUS  = 20;
static constexpr int ROW_H        = 90;
static constexpr int ROW_RADIUS   = 12;
static constexpr int ROW_GAP      = 10;
static constexpr int ROW_TOP      = 40;
static constexpr int ROW_INSET    = 20;
static constexpr int ROW_PAD      = 30;
static constexpr int PILL_H       = 36;
static constexpr int PILL_RADIUS  = 10;
static constexpr int HINT_SIZE    = 26;
static constexpr int HINT_ICON_SZ = 28;
static constexpr int HINT_GAP     = 5;
static constexpr int HINT_SPACING = 50;

struct SettingRow {
    const char* label;
    std::string description;
    bool value;
    std::string status;
    const char* valueLabel;
};

SettingsScreen::SettingsScreen()
    : mSelectedOption(0)
    , mShouldClose(false)
    , mSettingsChanged(false)
    , mFullFilesystemAccess(false)
    , mFtpServerEnabled(false)
    , mShowHiddenFiles(false)
    , mDateFormat(DateFormat::DayMonthYear)
    , mKeyboardType(KeyboardType::System)
    , mShowFtpResult(false)
    , mFtpModalOption(0)
{
    LoadSettings();
}

void SettingsScreen::LoadSettings() {
    mFullFilesystemAccess = Settings::GetFullFilesystemAccess();
    mFtpServerEnabled = Settings::GetFtpServerEnabled();
    mShowHiddenFiles = Settings::GetShowHiddenFiles();
    mDateFormat = Settings::GetDateFormat();
    mKeyboardType = Settings::GetKeyboardType();
}

void SettingsScreen::SaveSettings() {
    Settings::SetFullFilesystemAccess(mFullFilesystemAccess);
    Settings::SetFtpServerEnabled(mFtpServerEnabled);
    Settings::SetShowHiddenFiles(mShowHiddenFiles);
    Settings::SetDateFormat(mDateFormat);
    Settings::SetKeyboardType(mKeyboardType);
    Settings::Save();
    mSettingsChanged = true;
    WHBLogPrintf("Settings saved: full_filesystem_access=%d ftp_server_enabled=%d show_hidden_files=%d date_format=%s keyboard_type=%s",
                 mFullFilesystemAccess, mFtpServerEnabled, mShowHiddenFiles,
                 mDateFormat == DateFormat::DayMonthYear ? "DD/MM/YYYY" : "MM/DD/YYYY",
                 mKeyboardType == KeyboardType::Custom ? "Custom" : "System");
}

void SettingsScreen::ToggleFullFilesystemAccess() {
    mFullFilesystemAccess = !mFullFilesystemAccess;
    WHBLogPrintf("Toggled full filesystem access: %d", mFullFilesystemAccess);
    SaveSettings();
}

void SettingsScreen::ToggleShowHiddenFiles() {
    mShowHiddenFiles = !mShowHiddenFiles;
    WHBLogPrintf("Toggled show hidden files: %d", mShowHiddenFiles);
    SaveSettings();
}

void SettingsScreen::ToggleDateFormat() {
    mDateFormat = (mDateFormat == DateFormat::DayMonthYear)
                      ? DateFormat::MonthDayYear
                      : DateFormat::DayMonthYear;
    WHBLogPrintf("Toggled date format: %s",
                 mDateFormat == DateFormat::DayMonthYear ? "DD/MM/YYYY" : "MM/DD/YYYY");
    SaveSettings();
}

void SettingsScreen::ToggleKeyboardType() {
    mKeyboardType = (mKeyboardType == KeyboardType::System)
                      ? KeyboardType::Custom
                      : KeyboardType::System;
    WHBLogPrintf("Toggled keyboard type: %s",
                 mKeyboardType == KeyboardType::Custom ? "Custom" : "System");
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

        Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, SDL_Color{0, 0, 0, 180});
        Gfx::DrawPanel(modalX, modalY, modalWidth, modalHeight);

        Gfx::Print(modalX + modalWidth / 2, modalY + 50, 28, Gfx::COLOR_WHITE,
                   "FTP Server Running", Gfx::ALIGN_CENTER);

        std::string ipText = "IP: " + mFtpResultIP + " : Port: " + std::to_string(FtpServer::GetPort());
        Gfx::Print(modalX + modalWidth / 2, modalY + 120, 30, Gfx::COLOR_WHITE,
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
        Gfx::Print(bgX + buttonWidth / 2, buttonY + buttonHeight / 2 + 5, 26,
                   Gfx::COLOR_WHITE, "Background (A)", Gfx::ALIGN_CENTER);

        Gfx::DrawRectFilled(stopX, buttonY, buttonWidth, buttonHeight, stopColor);
        Gfx::Print(stopX + buttonWidth / 2, buttonY + buttonHeight / 2 + 5, 26,
                   Gfx::COLOR_WHITE, "Stop (B)", Gfx::ALIGN_CENTER);

        DrawCenteredHints("A: Confirm", modalX + modalWidth / 2,
                          modalY + modalHeight - 30, 26, 20);
        return;
    }

    DrawTopBar("Settings");
    Gfx::Print(sTopBarClockLeft - 30, 40, 26, Gfx::COLOR_TEXT, "v3.0", Gfx::ALIGN_RIGHT | Gfx::ALIGN_VERTICAL);

    const bool ftpRunning = FtpServer::IsRunning();
    const SettingRow rows[] = {
        { "Full Wii U Filesystem Access", "Access all system storage", mFullFilesystemAccess, "", nullptr },
        { "FTP Server",
          ftpRunning ? "FTP active - toggle OFF to stop"
                     : "Start FTP server on port " + std::to_string(FtpServer::GetPort()),
          mFtpServerEnabled,
          ftpRunning ? "Running..." : "",
          nullptr },
        { "Show Hidden Files", "Show files and folders starting with '.'", mShowHiddenFiles, "", nullptr },
        { "Date Format",
          "Order of day and month in dates",
          mDateFormat == DateFormat::DayMonthYear,
          "",
          mDateFormat == DateFormat::DayMonthYear ? "DD/MM/YYYY" : "MM/DD/YYYY" },
        { "Keyboard",
          "On-screen keyboard used for typing names",
          mKeyboardType == KeyboardType::Custom,
          "",
          mKeyboardType == KeyboardType::Custom ? "Custom" : "System" },
    };
    const int rowCount = static_cast<int>(sizeof(rows) / sizeof(rows[0]));

    const int cardX = (static_cast<int>(Gfx::SCREEN_WIDTH) - CARD_W) / 2;
    const int cardY = (static_cast<int>(Gfx::SCREEN_HEIGHT) - CARD_H) / 2;

    Gfx::DrawRectFilled(cardX + 4, cardY + 4, CARD_W, CARD_H, SDL_Color{0x00, 0x00, 0x00, 0x30});
    Gfx::DrawPanel(cardX, cardY, CARD_W, CARD_H, CARD_RADIUS);

    const int rowX = cardX + ROW_INSET;
    const int rowW = CARD_W - 2 * ROW_INSET;

    for (int i = 0; i < rowCount; i++) {
        const SettingRow& row = rows[i];
        const bool selected = (i == mSelectedOption);
        const int rowY = cardY + ROW_TOP + i * (ROW_H + ROW_GAP);
        const SDL_Color rowBg = selected ? Gfx::COLOR_HIGHLIGHTED : Gfx::COLOR_BACKGROUND;

        Gfx::DrawRectRounded(rowX, rowY, rowW, ROW_H, ROW_RADIUS, rowBg);

        if (selected) {
            const float pulse = std::sin(static_cast<float>(SDL_GetTicks()) / 300.0f) * 0.3f + 0.7f;
            const uint8_t alpha = static_cast<uint8_t>(180.0f * pulse + 75.0f);
            const SDL_Color accent = Gfx::COLOR_ACCENT;
            Gfx::DrawRoundedOutline(rowX, rowY, rowW, ROW_H, ROW_RADIUS,
                                    SDL_Color{accent.r, accent.g, accent.b, alpha}, 3, rowBg);
            Gfx::DrawRectFilled(rowX + 4, rowY, 6, ROW_H, Gfx::COLOR_ACCENT);
        }

        Gfx::Print(rowX + ROW_PAD, rowY + 32, 30, Gfx::COLOR_TEXT,
                   row.label, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
        Gfx::Print(rowX + ROW_PAD, rowY + 64, 20, Gfx::COLOR_ALT_TEXT,
                   row.description, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);

        if (!row.status.empty()) {
            const int statusX = rowX + ROW_PAD + Gfx::GetTextWidth(30, row.label) + 20;
            Gfx::Print(statusX, rowY + 32, 22, Gfx::COLOR_WHITE,
                       row.status, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
        }

        const char* valueText = row.valueLabel ? row.valueLabel : (row.value ? "On" : "Off");
        const int pillW = Gfx::GetTextWidth(26, valueText) + 24;
        const int pillX = rowX + rowW - ROW_PAD - pillW;
        Gfx::DrawRectRounded(pillX, rowY + (ROW_H - PILL_H) / 2, pillW, PILL_H,
                             PILL_RADIUS, Gfx::COLOR_BARS);
        Gfx::Print(pillX + pillW / 2, rowY + ROW_H / 2, 26, Gfx::COLOR_WHITE,
                   valueText, Gfx::ALIGN_HORIZONTAL | Gfx::ALIGN_VERTICAL);
    }

    struct Hint {
        const char* glyph;
        const char* label;
    };
    const Hint hints[] = {
        { "\xee\x80\x80", "Toggle" },
        { "\xee\x80\x81", "Back"   },
    };
    const int hintCount = static_cast<int>(sizeof(hints) / sizeof(hints[0]));
    const int hintY = cardY + ROW_TOP + rowCount * (ROW_H + ROW_GAP) + 30;

    int hintsWidth = 0;
    for (int i = 0; i < hintCount; i++) {
        hintsWidth += Gfx::GetIconTextWidth(HINT_ICON_SZ, hints[i].glyph) + HINT_GAP +
                      Gfx::GetTextWidth(HINT_SIZE, hints[i].label);
        if (i + 1 < hintCount) hintsWidth += HINT_SPACING;
    }

    int hintX = cardX + CARD_W / 2 - hintsWidth / 2;
    for (int i = 0; i < hintCount; i++) {
        const int iconW = Gfx::GetIconTextWidth(HINT_ICON_SZ, hints[i].glyph);
        Gfx::PrintIcon(hintX, hintY, HINT_ICON_SZ, Gfx::COLOR_BARS,
                       hints[i].glyph, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
        hintX += iconW + HINT_GAP;
        const int labelW = Gfx::GetTextWidth(HINT_SIZE, hints[i].label);
        Gfx::Print(hintX, hintY, HINT_SIZE, Gfx::COLOR_WHITE,
                   hints[i].label, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
        hintX += labelW;
        if (i + 1 < hintCount) hintX += HINT_SPACING;
    }

    DrawBottomBar(nullptr, nullptr, "HOME: Exit");
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
        return true;
    }

    if (input.data.buttons_d & Input::BUTTON_B) {
        mShouldClose = true;
        return true;
    }

    constexpr int OPTION_COUNT = 5;

    if (input.data.buttons_d & Input::BUTTON_DOWN) {
        mSelectedOption = (mSelectedOption + 1) % OPTION_COUNT;
    }
    if (input.data.buttons_d & Input::BUTTON_UP) {
        mSelectedOption = (mSelectedOption - 1 + OPTION_COUNT) % OPTION_COUNT;
    }

    if (input.data.buttons_d & Input::BUTTON_A) {
        if (mSelectedOption == 0) {
            ToggleFullFilesystemAccess();
        } else if (mSelectedOption == 1) {
            ToggleFtpServer();
        } else if (mSelectedOption == 2) {
            ToggleShowHiddenFiles();
        } else if (mSelectedOption == 3) {
            ToggleDateFormat();
        } else if (mSelectedOption == 4) {
            ToggleKeyboardType();
        }
    }

    return true;
}

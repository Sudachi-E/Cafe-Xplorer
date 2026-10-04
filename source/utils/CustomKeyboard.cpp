#include "CustomKeyboard.hpp"
#include "../Gfx.hpp"
#include "../input/Input.h"
#include <SDL.h>
#include <cmath>
#include <vector>

namespace {

    constexpr int PANEL_W      = 1760;
    constexpr int PANEL_H      = 600;
    constexpr int PANEL_RADIUS = 20;
    constexpr int PANEL_PAD    = 40;
    constexpr int PANEL_X      = (static_cast<int>(Gfx::SCREEN_WIDTH) - PANEL_W) / 2;
    constexpr int PANEL_Y      = (static_cast<int>(Gfx::SCREEN_HEIGHT) - PANEL_H) / 2;

    constexpr int FIELD_OFFSET = 30;
    constexpr int FIELD_H      = 86;
    constexpr int FIELD_RADIUS = 12;
    constexpr int FIELD_FONT   = 38;
    constexpr int FIELD_PAD    = 24;

    constexpr int KEYS_OFFSET  = 140;
    constexpr int KEY_H        = 68;
    constexpr int KEY_RADIUS   = 10;
    constexpr int KEY_GAP      = 10;
    constexpr int KEY_FONT     = 30;
    constexpr int ACTION_FONT  = 24;
    constexpr int MAX_ROWS     = 5;

    constexpr int HINT_OFFSET  = 545;
    constexpr int HINT_SIZE    = 24;
    constexpr int HINT_ICON_SZ = 26;
    constexpr int HINT_GAP     = 6;
    constexpr int HINT_SPACING = 28;

    constexpr size_t MAX_LEN = 255;

    constexpr uint32_t REPEAT_DELAY      = 300;
    constexpr uint32_t REPEAT_FAST       = 120;
    constexpr uint32_t REPEAT_RAPID      = 40;
    constexpr uint32_t REPEAT_RAPID_AFTER = 1500;

    enum class KeyType {
        Character,
        Shift,
        Backspace,
        Space,
        Layer,
        Cancel,
        Confirm,
    };

    struct Key {
        std::string label;
        std::string shifted;
        KeyType     type   = KeyType::Character;
        int         weight = 1;
        int         x = 0;
        int         y = 0;
        int         w = 0;
        int         h = KEY_H;
    };

    struct KeyDef {
        std::string label;
        std::string shifted;
        KeyType     type   = KeyType::Character;
        int         weight = 1;
    };

    using KeyRow = std::vector<Key>;

    struct RepeatState {
        uint32_t mask      = 0;
        uint32_t nextTick  = 0;
        uint32_t holdStart = 0;
    };

    const char* const kDigits      = "1234567890";
    const char* const kDigitsShift = "!@#$%^&*()";
    const char* const kSymbolRows[] = {
        "._-+=/\\|@",
        "()[]{}<>!?%",
        "#$%^&*~;'\"`",
    };
    constexpr int kSymbolRowCount = static_cast<int>(sizeof(kSymbolRows) / sizeof(kSymbolRows[0]));

    const char* const kFilenameForbidden = "/\\:*?\"<>|";

    struct Hint {
        const char* glyph;
        const char* label;
    };
    const Hint kHints[] = {
        { "\xee\x80\x80", "Select" },
        { "\xee\x80\x81", "Delete" },
        { "\xee\x80\x83", "Space" },
        { "\xee\x82\x83\xee\x82\x84", "Move Cursor" },
        { "\xee\x81\x85\xee\x81\x86", "Shift" },
    };
    constexpr int kHintCount = static_cast<int>(sizeof(kHints) / sizeof(kHints[0]));

    bool sOpen = false;
    bool sRestricted = false;
    bool sSymbols = false;
    bool sShift = false;

    std::string sText;
    size_t sCursor = 0;
    std::string sHint;
    std::function<void(bool, const std::string&)> sCallback;

    std::vector<KeyRow> sRows;
    int sSelRow = 0;
    int sSelCol = 0;

    RepeatState sVertRepeat;
    RepeatState sHorzRepeat;

    bool AllowedInRestricted(char c) {
        for (int i = 0; kFilenameForbidden[i]; i++) {
            if (kFilenameForbidden[i] == c) return false;
        }
        return true;
    }

    void AddRow(std::vector<KeyDef>&& defs) {
        KeyRow row;
        row.reserve(defs.size());
        for (const KeyDef& def : defs) {
            row.push_back(Key{def.label, def.shifted, def.type, def.weight, 0, 0, 0, KEY_H});
        }
        sRows.push_back(std::move(row));
    }

    void AddCharRow(std::vector<KeyDef>& defs, const char* lower, const char* upper) {
        for (int i = 0; lower[i]; i++) {
            defs.push_back(KeyDef{std::string(1, lower[i]),
                                  std::string(1, upper[i]),
                                  KeyType::Character, 1});
        }
    }

    void AddSymbolRow(const char* symbols) {
        std::vector<KeyDef> defs;
        for (int i = 0; symbols[i]; i++) {
            if (sRestricted && !AllowedInRestricted(symbols[i])) continue;
            defs.push_back(KeyDef{std::string(1, symbols[i]), "", KeyType::Character, 1});
        }
        if (!defs.empty()) AddRow(std::move(defs));
    }

    void AddActionRow() {
        AddRow({KeyDef{sSymbols ? "ABC" : "?123", "", KeyType::Layer, 3},
                KeyDef{"Space", "", KeyType::Space, 10},
                KeyDef{"Cancel", "", KeyType::Cancel, 3},
                KeyDef{"OK", "", KeyType::Confirm, 3}});
    }

    void Layout();

    void BuildRows() {
        sRows.clear();

        if (!sSymbols) {
            std::vector<KeyDef> digits;
            for (int i = 0; i < 10; i++) {
                digits.push_back(KeyDef{std::string(1, kDigits[i]),
                                        std::string(1, kDigitsShift[i]),
                                        KeyType::Character, 1});
            }
            AddRow(std::move(digits));

            std::vector<KeyDef> top;
            AddCharRow(top, "qwertyuiop", "QWERTYUIOP");
            AddRow(std::move(top));

            std::vector<KeyDef> home;
            AddCharRow(home, "asdfghjkl", "ASDFGHJKL");
            AddRow(std::move(home));

            std::vector<KeyDef> bottom;
            bottom.push_back(KeyDef{"Shift", "", KeyType::Shift, 3});
            AddCharRow(bottom, "zxcvbnm", "ZXCVBNM");
            bottom.push_back(KeyDef{"Back", "", KeyType::Backspace, 3});
            AddRow(std::move(bottom));
        } else {
            for (int i = 0; i < kSymbolRowCount; i++) {
                AddSymbolRow(kSymbolRows[i]);
            }
        }

        AddActionRow();
        Layout();
    }

    void Layout() {
        const int areaX = PANEL_X + PANEL_PAD;
        const int areaW = PANEL_W - 2 * PANEL_PAD;
        const int blockTop = PANEL_Y + KEYS_OFFSET +
                             ((MAX_ROWS - static_cast<int>(sRows.size())) * (KEY_H + KEY_GAP)) / 2;

        for (size_t r = 0; r < sRows.size(); r++) {
            KeyRow& row = sRows[r];

            int totalWeight = 0;
            for (const Key& key : row) totalWeight += key.weight;

            const int gaps  = KEY_GAP * (static_cast<int>(row.size()) - 1);
            const int usable = areaW - gaps;

            int used = 0;
            for (Key& key : row) {
                key.w = usable * key.weight / totalWeight;
                key.h = KEY_H;
                key.y = blockTop + static_cast<int>(r) * (KEY_H + KEY_GAP);
                used += key.w;
            }

            int x = areaX + (areaW - (used + gaps)) / 2;
            for (Key& key : row) {
                key.x = x;
                x += key.w + KEY_GAP;
            }
        }
    }

    void ResetSelection() {
        sSelRow = 0;
        sSelCol = 0;
    }

    void ClampSelection() {
        if (sRows.empty()) return;
        if (sSelRow >= static_cast<int>(sRows.size())) sSelRow = static_cast<int>(sRows.size()) - 1;
        if (sSelRow < 0) sSelRow = 0;
        const int colCount = static_cast<int>(sRows[sSelRow].size());
        if (sSelCol >= colCount) sSelCol = colCount - 1;
        if (sSelCol < 0) sSelCol = 0;
    }

    void MoveSelection(int rowStep, int colStep) {
        if (sRows.empty()) return;

        for (int i = 0; i < (rowStep > 0 ? rowStep : -rowStep); i++) {
            sSelRow += (rowStep > 0) ? 1 : -1;
        }
        for (int i = 0; i < (colStep > 0 ? colStep : -colStep); i++) {
            sSelCol += (colStep > 0) ? 1 : -1;
        }
        ClampSelection();
    }

    int RepeatStep(RepeatState& state, uint32_t now, uint32_t held, uint32_t pressed,
                   uint32_t negMask, uint32_t posMask) {
        const uint32_t axis = negMask | posMask;

        if (pressed & negMask) {
            state = RepeatState{held & axis, now + REPEAT_DELAY, now};
            return -1;
        }
        if (pressed & posMask) {
            state = RepeatState{held & axis, now + REPEAT_DELAY, now};
            return 1;
        }

        const uint32_t active = held & axis;
        if (active == 0 || active != state.mask || now < state.nextTick) {
            return 0;
        }

        const uint32_t interval =
            (now - state.holdStart < REPEAT_RAPID_AFTER) ? REPEAT_FAST : REPEAT_RAPID;
        state.nextTick = now + interval;

        if (active & posMask) return 1;
        if (active & negMask) return -1;
        return 0;
    }

    void InsertChar(const std::string& chars) {
        if (sText.size() + chars.size() > MAX_LEN) return;
        sText.insert(sCursor, chars);
        sCursor += chars.size();
    }

    void DeleteBackward() {
        if (sCursor == 0) return;
        sText.erase(sCursor - 1, 1);
        sCursor--;
    }

    void Finish(bool confirmed) {
        const std::string result = sText;
        std::function<void(bool, const std::string&)> callback = sCallback;
        sCallback = nullptr;
        sOpen     = false;
        if (callback) callback(confirmed, result);
    }

    const std::string& KeyLabel(const Key& key) {
        if (key.type == KeyType::Character && sShift && !key.shifted.empty()) {
            return key.shifted;
        }
        return key.label;
    }

    void PressKey(const Key& key) {
        switch (key.type) {
        case KeyType::Character:
            if (!key.label.empty()) InsertChar(KeyLabel(key));
            break;
        case KeyType::Shift:
            sShift = !sShift;
            break;
        case KeyType::Backspace:
            DeleteBackward();
            break;
        case KeyType::Space:
            InsertChar(" ");
            break;
        case KeyType::Layer:
            sSymbols = !sSymbols;
            sShift   = false;
            BuildRows();
            ResetSelection();
            return;
        case KeyType::Cancel:
            Finish(false);
            return;
        case KeyType::Confirm:
            Finish(true);
            return;
        }
    }

    void DrawHints() {
        const int y = PANEL_Y + HINT_OFFSET;

        int total = 0;
        for (int i = 0; i < kHintCount; i++) {
            total += Gfx::GetIconTextWidth(HINT_ICON_SZ, kHints[i].glyph) + HINT_GAP +
                     Gfx::GetTextWidth(HINT_SIZE, kHints[i].label);
            if (i + 1 < kHintCount) total += HINT_SPACING;
        }

        int x = PANEL_X + PANEL_W / 2 - total / 2;
        for (int i = 0; i < kHintCount; i++) {
            const int iconW = Gfx::GetIconTextWidth(HINT_ICON_SZ, kHints[i].glyph);
            Gfx::PrintIcon(x, y, HINT_ICON_SZ, Gfx::COLOR_ALT_TEXT,
                           kHints[i].glyph, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
            x += iconW + HINT_GAP;
            const int labelW = Gfx::GetTextWidth(HINT_SIZE, kHints[i].label);
            Gfx::Print(x, y, HINT_SIZE, Gfx::COLOR_TEXT,
                       kHints[i].label, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
            x += labelW;
            if (i + 1 < kHintCount) x += HINT_SPACING;
        }
    }

}

void CustomKeyboard::Open(const std::string& initialText,
                          const std::string& hint,
                          bool restricted,
                          std::function<void(bool, const std::string&)> callback) {
    sOpen       = true;
    sRestricted = restricted;
    sSymbols    = false;
    sShift      = false;
    sHint       = hint;
    sCallback   = std::move(callback);

    sText   = initialText.substr(0, MAX_LEN);
    sCursor = sText.size();

    sVertRepeat = RepeatState{};
    sHorzRepeat = RepeatState{};

    BuildRows();
    ResetSelection();
}

void CustomKeyboard::Close() {
    sOpen     = false;
    sCallback = nullptr;
    sText.clear();
    sCursor = 0;
}

bool CustomKeyboard::IsOpen() {
    return sOpen;
}

const std::string& CustomKeyboard::GetText() {
    return sText;
}

void CustomKeyboard::Update(uint32_t buttonsHeld, uint32_t buttonsPressed) {
    if (!sOpen) return;

    const uint32_t now = SDL_GetTicks();

    if (buttonsPressed & Input::BUTTON_L) {
        if (sCursor > 0) sCursor--;
    }
    if (buttonsPressed & Input::BUTTON_R) {
        if (sCursor < sText.size()) sCursor++;
    }

    const int rowStep = RepeatStep(sVertRepeat, now, buttonsHeld, buttonsPressed,
                                   Input::BUTTON_UP, Input::BUTTON_DOWN);
    const int colStep = RepeatStep(sHorzRepeat, now, buttonsHeld, buttonsPressed,
                                   Input::BUTTON_LEFT, Input::BUTTON_RIGHT);
    if (rowStep != 0 || colStep != 0) {
        MoveSelection(rowStep, colStep);
    }

    if (buttonsPressed & (Input::BUTTON_PLUS | Input::BUTTON_MINUS)) {
        sShift = !sShift;
    }

    if (buttonsPressed & Input::BUTTON_B) {
        DeleteBackward();
    }

    if (buttonsPressed & Input::BUTTON_Y) {
        InsertChar(" ");
    }

    if (buttonsPressed & Input::BUTTON_A) {
        if (!sRows.empty()) {
            ClampSelection();
            const Key& key = sRows[sSelRow][sSelCol];
            PressKey(key);
        }
        if (!sOpen) return;
    }
}

void CustomKeyboard::Draw() {
    if (!sOpen) return;

    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, SDL_Color{0, 0, 0, 190});
    Gfx::DrawPanel(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, PANEL_RADIUS);

    const int fieldY  = PANEL_Y + FIELD_OFFSET;
    const int fieldX  = PANEL_X + PANEL_PAD;
    const int fieldW  = PANEL_W - 2 * PANEL_PAD;
    const int textX   = fieldX + FIELD_PAD;
    const int maxTextW = fieldW - 2 * FIELD_PAD;

    Gfx::DrawRectRounded(fieldX, fieldY, fieldW, FIELD_H, FIELD_RADIUS, Gfx::COLOR_BACKGROUND);

    if (sText.empty()) {
        Gfx::Print(textX, fieldY + FIELD_H / 2, HINT_SIZE + 2, Gfx::COLOR_ALT_TEXT,
                   sHint, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
    } else {
        std::string visible = sText.substr(0, sCursor);
        while (!visible.empty() && Gfx::GetTextWidth(FIELD_FONT, visible) > maxTextW) {
            visible.erase(visible.begin());
        }
        Gfx::Print(textX, fieldY + FIELD_H / 2, FIELD_FONT, Gfx::COLOR_TEXT,
                   visible, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
        if ((SDL_GetTicks() / 500) % 2 == 0) {
            const int caretX = textX + Gfx::GetTextWidth(FIELD_FONT, visible) + 4;
            Gfx::DrawRectFilled(caretX, fieldY + 18, 4, FIELD_H - 36, Gfx::COLOR_TEXT);
        }
    }

    const float    pulse      = std::sin(static_cast<float>(SDL_GetTicks()) / 300.0f) * 0.3f + 0.7f;
    const uint8_t  pulseAlpha = static_cast<uint8_t>(180.0f * pulse + 75.0f);

    for (size_t r = 0; r < sRows.size(); r++) {
        const KeyRow& row = sRows[r];
        for (size_t c = 0; c < row.size(); c++) {
            const Key& key = row[c];
            const bool selected = (static_cast<int>(r) == sSelRow && static_cast<int>(c) == sSelCol);
            const bool latched  = (key.type == KeyType::Shift && sShift) ||
                                  (key.type == KeyType::Layer && sSymbols);

            SDL_Color bg = Gfx::COLOR_BARS;
            if (latched)  bg = Gfx::COLOR_ACCENT;
            if (selected) bg = Gfx::COLOR_HIGHLIGHTED;

            Gfx::DrawRectRounded(key.x, key.y, key.w, key.h, KEY_RADIUS, bg);

            if (selected) {
                const SDL_Color accent = Gfx::COLOR_ACCENT;
                Gfx::DrawRoundedOutline(key.x, key.y, key.w, key.h, KEY_RADIUS,
                                        SDL_Color{accent.r, accent.g, accent.b, pulseAlpha}, 3, bg);
            }

            const int fontSize = (key.type == KeyType::Character) ? KEY_FONT : ACTION_FONT;
            Gfx::Print(key.x + key.w / 2, key.y + key.h / 2, fontSize, Gfx::COLOR_TEXT,
                       KeyLabel(key), Gfx::ALIGN_CENTER);
        }
    }

    DrawHints();
}
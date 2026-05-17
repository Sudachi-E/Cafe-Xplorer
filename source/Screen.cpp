#include "Screen.hpp"
#include "Gfx.hpp"
#include <cstring>
#include <vector>
#include <string>

struct ButtonGlyph {
    const char* token;
    const char* glyph;
};

static const ButtonGlyph kButtonGlyphs[] = {
    { "A",    "\xee\x80\x80" },
    { "B",    "\xee\x80\x81" },
    { "X",    "\xee\x80\x82" },
    { "Y",    "\xee\x80\x83" },
    { "L",    "\xee\x80\x84" },
    { "R",    "\xee\x80\x85" },
    { "ZL",   "\xee\x82\x85" },
    { "ZR",   "\xee\x82\x86" },
    { "HOME", "\xee\x81\x84" },
    { "+",    "\xee\x81\x85" },
    { "-",    "\xee\x81\x86" },
    { "PageL/PageR", "\xee\x82\x83\xee\x82\x84" },
    { "Zoom",  "\xee\x82\x82" },
    { "Stick", "\xee\x82\x81" },
    { "L/R",  "\xee\x82\x83\xee\x82\x84" },
    { "ZL/ZR","\xee\x82\x85\xee\x82\x86" },
};

static const char* FindGlyph(const std::string& token) {
    for (const auto& bg : kButtonGlyphs) {
        if (token == bg.token) return bg.glyph;
    }
    return nullptr;
}

struct HintEntry {
    std::string glyph;
    std::string label;
};

static std::vector<HintEntry> ParseHints(const char* hint) {
    std::vector<HintEntry> entries;
    if (!hint || hint[0] == '\0') return entries;

    std::string s(hint);

    size_t pos = 0;
    while (pos < s.size()) {
        while (pos < s.size() && s[pos] == ' ') pos++;
        if (pos >= s.size()) break;

        size_t colon = s.find(':', pos);
        if (colon == std::string::npos) {
            std::string label = s.substr(pos);
            while (!label.empty() && label.back() == ' ') label.pop_back();
            if (!label.empty()) entries.push_back({"", label});
            break;
        }

        std::string token = s.substr(pos, colon - pos);
        while (!token.empty() && token.back() == ' ') token.pop_back();

        size_t labelStart = colon + 1;
        while (labelStart < s.size() && s[labelStart] == ' ') labelStart++;

        size_t labelEnd = labelStart;
        while (labelEnd < s.size()) {
            if (s[labelEnd] == '|') break;
            if (labelEnd + 1 < s.size() && s[labelEnd] == ' ' && s[labelEnd + 1] == ' ') break;
            labelEnd++;
        }

        std::string label = s.substr(labelStart, labelEnd - labelStart);
        while (!label.empty() && label.back() == ' ') label.pop_back();

        const char* g = FindGlyph(token);
        entries.push_back({ g ? g : "", label });

        pos = labelEnd;
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '|')) pos++;
    }

    return entries;
}

static int MeasureHints(const std::vector<HintEntry>& hints,
                        int iconSz, int txtSz, int gap, int spacing) {
    int total = 0;
    for (size_t i = 0; i < hints.size(); i++) {
        if (!hints[i].glyph.empty())
            total += Gfx::GetIconTextWidth(iconSz, hints[i].glyph) + gap;
        total += Gfx::GetTextWidth(txtSz, hints[i].label);
        if (i + 1 < hints.size()) total += spacing;
    }
    return total;
}

static int DrawHints(const std::vector<HintEntry>& hints,
                     int x, int y,
                     int iconSz, int txtSz, int gap, int spacing) {
    for (size_t i = 0; i < hints.size(); i++) {
        if (!hints[i].glyph.empty()) {
            int iw = Gfx::GetIconTextWidth(iconSz, hints[i].glyph);
            Gfx::PrintIcon(x, y, iconSz, Gfx::COLOR_ALT_TEXT,
                           hints[i].glyph, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
            x += iw + gap;
        }
        int tw = Gfx::GetTextWidth(txtSz, hints[i].label);
        Gfx::Print(x, y, txtSz, Gfx::COLOR_TEXT,
                   hints[i].label, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
        x += tw;
        if (i + 1 < hints.size()) x += spacing;
    }
    return x;
}

void Screen::DrawTopBar(const char *title) {
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, 80, Gfx::COLOR_BARS);

    Gfx::DrawRectFilled(0, 77, Gfx::SCREEN_WIDTH, 3, Gfx::COLOR_ACCENT);

    if (title) {
        Gfx::Print(40, 40, 48, Gfx::COLOR_TEXT, title, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
    }
}

void Screen::DrawBottomBar(const char *leftHint, const char *centerHint, const char *rightHint) {
    constexpr int BAR_Y    = Gfx::SCREEN_HEIGHT - 80;
    constexpr int BAR_H    = 80;
    constexpr int MID_Y    = Gfx::SCREEN_HEIGHT - 40;
    constexpr int ICON_SZ  = 34;
    constexpr int TXT_SZ   = 26;
    constexpr int GAP      = 6;
    constexpr int SPACING  = 28;
    constexpr int MARGIN   = 40;

    Gfx::DrawRectFilled(0, BAR_Y, Gfx::SCREEN_WIDTH, BAR_H, Gfx::COLOR_BARS);

    Gfx::DrawRectFilled(0, BAR_Y, Gfx::SCREEN_WIDTH, 3, Gfx::COLOR_ACCENT);

    if (leftHint && leftHint[0] != '\0') {
        auto hints = ParseHints(leftHint);
        DrawHints(hints, MARGIN, MID_Y, ICON_SZ, TXT_SZ, GAP, SPACING);
    }

    if (centerHint && centerHint[0] != '\0') {
        auto hints = ParseHints(centerHint);
        int w = MeasureHints(hints, ICON_SZ, TXT_SZ, GAP, SPACING);
        DrawHints(hints, Gfx::SCREEN_WIDTH / 2 - w / 2, MID_Y,
                  ICON_SZ, TXT_SZ, GAP, SPACING);
    }

    if (rightHint && rightHint[0] != '\0') {
        auto hints = ParseHints(rightHint);
        int w = MeasureHints(hints, ICON_SZ, TXT_SZ, GAP, SPACING);
        DrawHints(hints, Gfx::SCREEN_WIDTH - MARGIN - w, MID_Y,
                  ICON_SZ, TXT_SZ, GAP, SPACING);
    }
}

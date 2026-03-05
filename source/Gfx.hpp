#pragma once

#include <SDL.h>
#include <string>

namespace Gfx {
    constexpr uint32_t SCREEN_WIDTH  = 1920;
    constexpr uint32_t SCREEN_HEIGHT = 1080;

    constexpr SDL_Color COLOR_BLACK          = {0x00, 0x00, 0x00, 0xff};
    constexpr SDL_Color COLOR_WHITE          = {0xff, 0xff, 0xff, 0xff};
    constexpr SDL_Color COLOR_BACKGROUND     = {0xd4, 0xa5, 0x74, 0xff}; // Light brown
    constexpr SDL_Color COLOR_ALT_BACKGROUND = {0xc9, 0x9a, 0x6a, 0xff}; // Slightly darker light brown
    constexpr SDL_Color COLOR_HIGHLIGHTED    = {0x8b, 0x5a, 0x3c, 0xff}; // Medium brown for highlights
    constexpr SDL_Color COLOR_TEXT           = {0xf8, 0xf8, 0xf8, 0xff};
    constexpr SDL_Color COLOR_ALT_TEXT       = {0xe0, 0xd0, 0xc0, 0xff}; // Light beige for alt text
    constexpr SDL_Color COLOR_BARS           = {0x5c, 0x3d, 0x2e, 0xff}; // Dark brown

    enum AlignFlags {
        ALIGN_LEFT       = 1 << 0,
        ALIGN_RIGHT      = 1 << 1,
        ALIGN_HORIZONTAL = 1 << 2,
        ALIGN_TOP        = 1 << 3,
        ALIGN_BOTTOM     = 1 << 4,
        ALIGN_VERTICAL   = 1 << 5,
        ALIGN_CENTER     = ALIGN_HORIZONTAL | ALIGN_VERTICAL,
    };

    static constexpr inline AlignFlags operator|(AlignFlags lhs, AlignFlags rhs) {
        return static_cast<AlignFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    bool Init();
    void Shutdown();
    void Clear(SDL_Color color);
    void Render();
    void DrawRectFilled(int x, int y, int w, int h, SDL_Color color);
    void Print(int x, int y, int size, SDL_Color color, const std::string& text, AlignFlags align = ALIGN_LEFT | ALIGN_TOP);
    int GetTextWidth(int size, const std::string& text);
    int GetTextHeight(int size, const std::string& text);
    SDL_Renderer* GetRenderer();
}

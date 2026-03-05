#include "Screen.hpp"
#include "Gfx.hpp"

void Screen::DrawTopBar(const char *title) {
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, 80, Gfx::COLOR_BARS);
    
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, 40, 48, Gfx::COLOR_TEXT, "Cafe-Xplorer", Gfx::ALIGN_CENTER);
    
    if (title) {
        Gfx::Print(40, 40, 48, Gfx::COLOR_TEXT, title, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
    }
}

void Screen::DrawBottomBar(const char *leftHint, const char *centerHint, const char *rightHint) {
    Gfx::DrawRectFilled(0, Gfx::SCREEN_HEIGHT - 80, Gfx::SCREEN_WIDTH, 80, Gfx::COLOR_BARS);
    
    if (leftHint) {
        Gfx::Print(40, Gfx::SCREEN_HEIGHT - 40, 32, Gfx::COLOR_ALT_TEXT, leftHint, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
    }
    
    if (centerHint) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT - 40, 32, Gfx::COLOR_ALT_TEXT, centerHint, Gfx::ALIGN_CENTER);
    }
    
    if (rightHint) {
        Gfx::Print(Gfx::SCREEN_WIDTH - 40, Gfx::SCREEN_HEIGHT - 40, 32, Gfx::COLOR_ALT_TEXT, rightHint, Gfx::ALIGN_RIGHT | Gfx::ALIGN_VERTICAL);
    }
}

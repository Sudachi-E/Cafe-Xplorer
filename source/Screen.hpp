#pragma once

#include "input/Input.h"

class Screen {
public:
    Screen() = default;
    virtual ~Screen() = default;

    virtual void Draw() = 0;
    virtual bool Update(Input &input) = 0;

    static void SetActiveControllerType(Input::eControllerType type) { sActiveControllerType = type; }
    static Input::eControllerType GetActiveControllerType() { return sActiveControllerType; }

    static constexpr int TOP_BAR_HEIGHT    = 80;
    static constexpr int BOTTOM_BAR_HEIGHT = 80;

protected:
    static void DrawTopBar(const char *title);
    static void DrawBottomBar(const char *leftHint, const char *centerHint, const char *rightHint);

    static void DrawCenteredHints(const char *hint, int centerX, int y,
                                  int iconSize, int textSize);

    static int sTopBarClockLeft;

    static Input::eControllerType sActiveControllerType;
};

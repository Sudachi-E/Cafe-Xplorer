#pragma once

#include "input/Input.h"

class Screen {
public:
    Screen() = default;
    virtual ~Screen() = default;

    virtual void Draw() = 0;
    virtual bool Update(Input &input) = 0;

protected:
    static void DrawTopBar(const char *title);
    static void DrawBottomBar(const char *leftHint, const char *centerHint, const char *rightHint);
};

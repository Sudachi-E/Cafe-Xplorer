#pragma once
#include "Input.h"
#include <vpad/input.h>

class VPadInput : public Input {
public:
    bool update(int32_t width, int32_t height) {
        VPADStatus vpad{};
        VPADReadError error;
        VPADRead(VPAD_CHAN_0, &vpad, 1, &error);

        if (error != VPAD_READ_SUCCESS) {
            return false;
        }

        data.buttons_h = MapVPADButtons(vpad.hold);
        data.buttons_d = MapVPADButtons(vpad.trigger);
        data.buttons_r = MapVPADButtons(vpad.release);
        data.leftStickX = vpad.leftStick.x;
        data.leftStickY = vpad.leftStick.y;
        data.rightStickX = vpad.rightStick.x;
        data.rightStickY = vpad.rightStick.y;
        return true;
    }

private:
    static uint32_t MapVPADButtons(uint32_t v) {
        uint32_t m = 0;
        if (v & VPAD_BUTTON_A)     m |= Input::BUTTON_A;
        if (v & VPAD_BUTTON_B)     m |= Input::BUTTON_B;
        if (v & VPAD_BUTTON_X)     m |= Input::BUTTON_X;
        if (v & VPAD_BUTTON_Y)     m |= Input::BUTTON_Y;
        if (v & VPAD_BUTTON_LEFT)  m |= Input::BUTTON_LEFT;
        if (v & VPAD_BUTTON_RIGHT) m |= Input::BUTTON_RIGHT;
        if (v & VPAD_BUTTON_UP)    m |= Input::BUTTON_UP;
        if (v & VPAD_BUTTON_DOWN)  m |= Input::BUTTON_DOWN;
        if (v & VPAD_BUTTON_L)     m |= Input::BUTTON_L;
        if (v & VPAD_BUTTON_R)     m |= Input::BUTTON_R;
        if (v & VPAD_BUTTON_PLUS)  m |= Input::BUTTON_PLUS;
        if (v & VPAD_BUTTON_MINUS) m |= Input::BUTTON_MINUS;
        if (v & VPAD_BUTTON_ZL)    m |= Input::BUTTON_ZL;
        if (v & VPAD_BUTTON_ZR)    m |= Input::BUTTON_ZR;
        return m;
    }
};

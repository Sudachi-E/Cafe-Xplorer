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

        data.buttons_h = vpad.hold;
        data.buttons_d = vpad.trigger;
        data.buttons_r = vpad.release;
        data.leftStickX = vpad.leftStick.x;
        data.leftStickY = vpad.leftStick.y;
        data.rightStickX = vpad.rightStick.x;
        data.rightStickY = vpad.rightStick.y;
        return true;
    }
};

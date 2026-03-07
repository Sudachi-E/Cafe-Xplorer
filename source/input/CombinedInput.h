#pragma once
#include "Input.h"

class CombinedInput : public Input {
public:
    void combine(const Input &b) {
        data.buttons_h |= b.data.buttons_h;
        data.buttons_d |= b.data.buttons_d;
        data.buttons_r |= b.data.buttons_r;
        if (b.data.leftStickX != 0.0f || b.data.leftStickY != 0.0f) {
            data.leftStickX = b.data.leftStickX;
            data.leftStickY = b.data.leftStickY;
        }
        if (b.data.rightStickX != 0.0f || b.data.rightStickY != 0.0f) {
            data.rightStickX = b.data.rightStickX;
            data.rightStickY = b.data.rightStickY;
        }
    }

    void process() {
        lastData.buttons_h = data.buttons_h;
    }

    void reset() {
        data.buttons_h = 0;
        data.buttons_d = 0;
        data.buttons_r = 0;
        data.leftStickX = 0.0f;
        data.leftStickY = 0.0f;
        data.rightStickX = 0.0f;
        data.rightStickY = 0.0f;
    }
};

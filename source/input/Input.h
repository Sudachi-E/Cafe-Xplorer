#pragma once

#include <cstdint>

class Input {
public:
    Input() = default;
    virtual ~Input() = default;

    enum eButtons {
        BUTTON_NONE   = 0x0000,
        BUTTON_A      = 0x8000,
        BUTTON_B      = 0x4000,
        BUTTON_X      = 0x2000,
        BUTTON_Y      = 0x1000,
        BUTTON_LEFT   = 0x0800,
        BUTTON_RIGHT  = 0x0400,
        BUTTON_UP     = 0x0200,
        BUTTON_DOWN   = 0x0100,
        BUTTON_L      = 0x0020,
        BUTTON_R      = 0x0010,
        BUTTON_HOME   = 0x0002,
    };

    typedef struct {
        uint32_t buttons_h;
        uint32_t buttons_d;
        uint32_t buttons_r;
        float leftStickX;
        float leftStickY;
        float rightStickX;
        float rightStickY;
    } PadData;

    PadData data{};
    PadData lastData{};
};

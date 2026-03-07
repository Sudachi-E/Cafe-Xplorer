#pragma once
#include "Input.h"
#include <padscore/wpad.h>
#include <padscore/kpad.h>

class WPADInput : public Input {
public:
    explicit WPADInput(WPADChan chan) : mChan(chan) {}

    bool update(int32_t width, int32_t height) {
        KPADStatus kpad{};
        KPADError error;

        if (KPADReadEx((KPADChan)mChan, &kpad, 1, &error) != 1 || error != KPAD_ERROR_OK) {
            return false;
        }

        data.buttons_h = kpad.hold;
        data.buttons_d = kpad.trigger;
        data.buttons_r = kpad.release;
        
        // Read analog sticks based on extension type
        if (kpad.extensionType == WPAD_EXT_CLASSIC || kpad.extensionType == WPAD_EXT_MPLUS_CLASSIC) {
            data.leftStickX = kpad.classic.leftStick.x;
            data.leftStickY = kpad.classic.leftStick.y;
            data.rightStickX = kpad.classic.rightStick.x;
            data.rightStickY = kpad.classic.rightStick.y;
        } else if (kpad.extensionType == WPAD_EXT_PRO_CONTROLLER) {
            data.leftStickX = kpad.pro.leftStick.x;
            data.leftStickY = kpad.pro.leftStick.y;
            data.rightStickX = kpad.pro.rightStick.x;
            data.rightStickY = kpad.pro.rightStick.y;
        } else {
            data.leftStickX = 0.0f;
            data.leftStickY = 0.0f;
            data.rightStickX = 0.0f;
            data.rightStickY = 0.0f;
        }
        
        return true;
    }

private:
    WPADChan mChan;
};

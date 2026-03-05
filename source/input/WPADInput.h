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
        return true;
    }

private:
    WPADChan mChan;
};

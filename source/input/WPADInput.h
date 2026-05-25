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

        if (kpad.extensionType == WPAD_EXT_PRO_CONTROLLER) {
            data.buttons_h = MapProButtons(kpad.pro.hold);
            data.buttons_d = MapProButtons(kpad.pro.trigger);
            data.buttons_r = MapProButtons(kpad.pro.release);
            data.leftStickX  = kpad.pro.leftStick.x;
            data.leftStickY  = kpad.pro.leftStick.y;
            data.rightStickX = kpad.pro.rightStick.x;
            data.rightStickY = kpad.pro.rightStick.y;
        } else if (kpad.extensionType == WPAD_EXT_CLASSIC || kpad.extensionType == WPAD_EXT_MPLUS_CLASSIC) {
            data.buttons_h = kpad.hold;
            data.buttons_d = kpad.trigger;
            data.buttons_r = kpad.release;
            data.leftStickX  = kpad.classic.leftStick.x;
            data.leftStickY  = kpad.classic.leftStick.y;
            data.rightStickX = kpad.classic.rightStick.x;
            data.rightStickY = kpad.classic.rightStick.y;
        } else if (kpad.extensionType == WPAD_EXT_CORE) {
            data.buttons_h = MapWiiButtons(kpad.hold);
            data.buttons_d = MapWiiButtons(kpad.trigger);
            data.buttons_r = MapWiiButtons(kpad.release);
            data.leftStickX  = 0.0f;
            data.leftStickY  = 0.0f;
            data.rightStickX = 0.0f;
            data.rightStickY = 0.0f;
        } else {
            data.buttons_h = kpad.hold;
            data.buttons_d = kpad.trigger;
            data.buttons_r = kpad.release;
            data.leftStickX  = 0.0f;
            data.leftStickY  = 0.0f;
            data.rightStickX = 0.0f;
            data.rightStickY = 0.0f;
        }

        return true;
    }

private:
    WPADChan mChan;

    static uint32_t MapWiiButtons(uint32_t p) {
        uint32_t m = 0;
        if (p & WPAD_BUTTON_LEFT)    m |= Input::BUTTON_LEFT;
        if (p & WPAD_BUTTON_RIGHT)   m |= Input::BUTTON_RIGHT;
        if (p & WPAD_BUTTON_DOWN)    m |= Input::BUTTON_DOWN;
        if (p & WPAD_BUTTON_UP)      m |= Input::BUTTON_UP;
        if (p & WPAD_BUTTON_PLUS)    m |= Input::BUTTON_PLUS;
        if (p & WPAD_BUTTON_B)       m |= Input::BUTTON_B;
        if (p & WPAD_BUTTON_A)       m |= Input::BUTTON_A;
        if (p & WPAD_BUTTON_MINUS)   m |= Input::BUTTON_MINUS;
        if (p & WPAD_BUTTON_HOME)    m |= Input::BUTTON_HOME;
        return m;
    }

    static uint32_t MapProButtons(uint32_t p) {
        uint32_t m = 0;
        if (p & WPAD_PRO_BUTTON_A)     m |= Input::BUTTON_A;
        if (p & WPAD_PRO_BUTTON_B)     m |= Input::BUTTON_B;
        if (p & WPAD_PRO_BUTTON_X)     m |= Input::BUTTON_X;
        if (p & WPAD_PRO_BUTTON_Y)     m |= Input::BUTTON_Y;
        if (p & WPAD_PRO_BUTTON_LEFT)  m |= Input::BUTTON_LEFT;
        if (p & WPAD_PRO_BUTTON_RIGHT) m |= Input::BUTTON_RIGHT;
        if (p & WPAD_PRO_BUTTON_UP)    m |= Input::BUTTON_UP;
        if (p & WPAD_PRO_BUTTON_DOWN)  m |= Input::BUTTON_DOWN;
        if (p & WPAD_PRO_BUTTON_L)     m |= Input::BUTTON_L;
        if (p & WPAD_PRO_BUTTON_R)     m |= Input::BUTTON_R;
        if (p & WPAD_PRO_BUTTON_PLUS)  m |= Input::BUTTON_HOME;
        if (p & WPAD_PRO_TRIGGER_ZL)   m |= Input::BUTTON_ZL;
        if (p & WPAD_PRO_TRIGGER_ZR)   m |= Input::BUTTON_ZR;
        if (p & WPAD_PRO_STICK_L_EMULATION_UP)    m |= Input::BUTTON_UP;
        if (p & WPAD_PRO_STICK_L_EMULATION_DOWN)  m |= Input::BUTTON_DOWN;
        if (p & WPAD_PRO_STICK_L_EMULATION_LEFT)  m |= Input::BUTTON_LEFT;
        if (p & WPAD_PRO_STICK_L_EMULATION_RIGHT) m |= Input::BUTTON_RIGHT;
        return m;
    }
};

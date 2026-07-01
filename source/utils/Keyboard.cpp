#include "Keyboard.hpp"
#include <SDL.h>
#include <SDL_system.h>
#include <SDL_syswm.h>
#include <nn/swkbd.h>
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <whb/log.h>

Keyboard::State Keyboard::sState            = Keyboard::State::Idle;
std::string     Keyboard::sInputBuffer;
bool            Keyboard::sPendingConfirmed = false;
std::function<void(bool, const std::string&)> Keyboard::sCallback;

static uint32_t MapWiimoteToVPADButtons(uint32_t wiimoteButtons) {
    uint32_t vpadButtons = 0;
    if (wiimoteButtons & WPAD_BUTTON_LEFT)   vpadButtons |= VPAD_BUTTON_LEFT;
    if (wiimoteButtons & WPAD_BUTTON_RIGHT)  vpadButtons |= VPAD_BUTTON_RIGHT;
    if (wiimoteButtons & WPAD_BUTTON_DOWN)   vpadButtons |= VPAD_BUTTON_DOWN;
    if (wiimoteButtons & WPAD_BUTTON_UP)     vpadButtons |= VPAD_BUTTON_UP;
    if (wiimoteButtons & WPAD_BUTTON_A)      vpadButtons |= VPAD_BUTTON_A;
    if (wiimoteButtons & WPAD_BUTTON_B)      vpadButtons |= VPAD_BUTTON_B;
    if (wiimoteButtons & WPAD_BUTTON_1)      vpadButtons |= VPAD_BUTTON_X;
    if (wiimoteButtons & WPAD_BUTTON_2)      vpadButtons |= VPAD_BUTTON_Y;
    if (wiimoteButtons & WPAD_BUTTON_PLUS)   vpadButtons |= VPAD_BUTTON_PLUS;
    if (wiimoteButtons & WPAD_BUTTON_MINUS)  vpadButtons |= VPAD_BUTTON_MINUS;
    if (wiimoteButtons & WPAD_BUTTON_HOME)   vpadButtons |= VPAD_BUTTON_HOME;
    return vpadButtons;
}

static uint32_t MapClassicToVPADButtons(uint32_t classicButtons) {
    uint32_t vpadButtons = 0;
    if (classicButtons & WPAD_CLASSIC_BUTTON_A)      vpadButtons |= VPAD_BUTTON_A;
    if (classicButtons & WPAD_CLASSIC_BUTTON_B)      vpadButtons |= VPAD_BUTTON_B;
    if (classicButtons & WPAD_CLASSIC_BUTTON_X)      vpadButtons |= VPAD_BUTTON_X;
    if (classicButtons & WPAD_CLASSIC_BUTTON_Y)      vpadButtons |= VPAD_BUTTON_Y;
    if (classicButtons & WPAD_CLASSIC_BUTTON_LEFT)   vpadButtons |= VPAD_BUTTON_LEFT;
    if (classicButtons & WPAD_CLASSIC_BUTTON_RIGHT)  vpadButtons |= VPAD_BUTTON_RIGHT;
    if (classicButtons & WPAD_CLASSIC_BUTTON_UP)     vpadButtons |= VPAD_BUTTON_UP;
    if (classicButtons & WPAD_CLASSIC_BUTTON_DOWN)   vpadButtons |= VPAD_BUTTON_DOWN;
    if (classicButtons & WPAD_CLASSIC_BUTTON_PLUS)   vpadButtons |= VPAD_BUTTON_PLUS;
    if (classicButtons & WPAD_CLASSIC_BUTTON_MINUS)  vpadButtons |= VPAD_BUTTON_MINUS;
    if (classicButtons & WPAD_CLASSIC_BUTTON_HOME)   vpadButtons |= VPAD_BUTTON_HOME;
    if (classicButtons & WPAD_CLASSIC_BUTTON_L)      vpadButtons |= VPAD_BUTTON_L;
    if (classicButtons & WPAD_CLASSIC_BUTTON_R)      vpadButtons |= VPAD_BUTTON_R;
    if (classicButtons & WPAD_CLASSIC_BUTTON_ZL)     vpadButtons |= VPAD_BUTTON_ZL;
    if (classicButtons & WPAD_CLASSIC_BUTTON_ZR)     vpadButtons |= VPAD_BUTTON_ZR;
    return vpadButtons;
}

static uint32_t MapNunchukToVPADButtons(uint32_t nunchukButtons) {
    uint32_t vpadButtons = 0;
    if (nunchukButtons & WPAD_NUNCHUK_BUTTON_C) vpadButtons |= VPAD_BUTTON_ZR;
    if (nunchukButtons & WPAD_NUNCHUK_BUTTON_Z) vpadButtons |= VPAD_BUTTON_ZL;
    return vpadButtons;
}

static uint32_t NunchukStickToVPADButtons(const KPADVec2D &stick) {
    uint32_t vpadButtons = 0;
    if (stick.x > 0.25f)  vpadButtons |= VPAD_BUTTON_RIGHT;
    if (stick.x < -0.25f) vpadButtons |= VPAD_BUTTON_LEFT;
    if (stick.y > 0.25f)  vpadButtons |= VPAD_BUTTON_UP;
    if (stick.y < -0.25f) vpadButtons |= VPAD_BUTTON_DOWN;
    return vpadButtons;
}

static uint32_t MapProToVPADButtons(uint32_t proButtons) {
    uint32_t vpadButtons = 0;
    if (proButtons & WPAD_PRO_BUTTON_A)      vpadButtons |= VPAD_BUTTON_A;
    if (proButtons & WPAD_PRO_BUTTON_B)      vpadButtons |= VPAD_BUTTON_B;
    if (proButtons & WPAD_PRO_BUTTON_X)      vpadButtons |= VPAD_BUTTON_X;
    if (proButtons & WPAD_PRO_BUTTON_Y)      vpadButtons |= VPAD_BUTTON_Y;
    if (proButtons & WPAD_PRO_BUTTON_LEFT)   vpadButtons |= VPAD_BUTTON_LEFT;
    if (proButtons & WPAD_PRO_BUTTON_RIGHT)  vpadButtons |= VPAD_BUTTON_RIGHT;
    if (proButtons & WPAD_PRO_BUTTON_UP)     vpadButtons |= VPAD_BUTTON_UP;
    if (proButtons & WPAD_PRO_BUTTON_DOWN)   vpadButtons |= VPAD_BUTTON_DOWN;
    if (proButtons & WPAD_PRO_BUTTON_PLUS)   vpadButtons |= VPAD_BUTTON_PLUS;
    if (proButtons & WPAD_PRO_BUTTON_MINUS)  vpadButtons |= VPAD_BUTTON_MINUS;
    if (proButtons & WPAD_PRO_BUTTON_HOME)   vpadButtons |= VPAD_BUTTON_HOME;
    if (proButtons & WPAD_PRO_BUTTON_L)      vpadButtons |= VPAD_BUTTON_L;
    if (proButtons & WPAD_PRO_BUTTON_R)      vpadButtons |= VPAD_BUTTON_R;
    if (proButtons & WPAD_PRO_TRIGGER_ZL)    vpadButtons |= VPAD_BUTTON_ZL;
    if (proButtons & WPAD_PRO_TRIGGER_ZR)    vpadButtons |= VPAD_BUTTON_ZR;
    if (proButtons & WPAD_PRO_STICK_L_EMULATION_LEFT)  vpadButtons |= VPAD_BUTTON_LEFT;
    if (proButtons & WPAD_PRO_STICK_L_EMULATION_RIGHT) vpadButtons |= VPAD_BUTTON_RIGHT;
    if (proButtons & WPAD_PRO_STICK_L_EMULATION_UP)    vpadButtons |= VPAD_BUTTON_UP;
    if (proButtons & WPAD_PRO_STICK_L_EMULATION_DOWN)  vpadButtons |= VPAD_BUTTON_DOWN;
    return vpadButtons;
}

static int SDLCALL KeyboardEventWatch(void* /*userdata*/, SDL_Event* event) {
    if (Keyboard::sState == Keyboard::State::Idle) {
        return 1;
    }

    if (event->type == SDL_TEXTINPUT) {
        if (Keyboard::sState == Keyboard::State::Visible) {
            Keyboard::sInputBuffer += event->text.text;
        }
        return 0;
    }

#if defined(SDL_VIDEO_DRIVER_WIIU)
    if (event->type == SDL_SYSWMEVENT) {
        unsigned wiiuEvent = event->syswm.msg->msg.wiiu.event;

        if (wiiuEvent == SDL_WIIU_SYSWM_SWKBD_OK_START_EVENT) {
            Keyboard::sInputBuffer.clear();
            return 0;
        }

        if (wiiuEvent == SDL_WIIU_SYSWM_SWKBD_OK_FINISH_EVENT) {
            Keyboard::sPendingConfirmed = true;
            Keyboard::sState = Keyboard::State::Disappearing;
            SDL_StopTextInput();
            return 0;
        }

        if (wiiuEvent == SDL_WIIU_SYSWM_SWKBD_CANCEL_EVENT) {
            Keyboard::sPendingConfirmed = false;
            Keyboard::sState = Keyboard::State::Disappearing;
            SDL_StopTextInput();
            return 0;
        }
    }
#endif

    return 1;
}

bool Keyboard::Init() {
    SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    SDL_AddEventWatch(KeyboardEventWatch, nullptr);
    sState = State::Idle;
    WHBLogPrintf("Keyboard::Init: complete");
    return true;
}

void Keyboard::Shutdown() {
    SDL_DelEventWatch(KeyboardEventWatch, nullptr);
    if (sState != State::Idle) {
        SDL_StopTextInput();
    }
    sCallback = nullptr;
    sState    = State::Idle;
}

bool Keyboard::RequestKeyboard(const std::string& initialText,
                               const std::string& hint,
                               std::function<void(bool, const std::string&)> callback,
                               SDL_WiiUSWKBDKeyboardMode mode) {
    if (sState != State::Idle) {
        return false;
    }

    sCallback         = callback;
    sInputBuffer      = "";
    sPendingConfirmed = false;

    SDL_WiiUSetSWKBDInitialText(initialText.empty() ? nullptr : initialText.c_str());
    SDL_WiiUSetSWKBDHintText(hint.empty() ? nullptr : hint.c_str());
    SDL_WiiUSetSWKBDHighlightInitialText(!initialText.empty() ? SDL_TRUE : SDL_FALSE);
    SDL_WiiUSetSWKBDShowCopyPasteButtons(SDL_TRUE);
    SDL_WiiUSetSWKBDKeyboardMode(mode);

    SDL_StartTextInput();
    sState = State::Visible;
    return true;
}

void Keyboard::Update() {
    if (sState == State::Idle) return;

    VPADStatus    vpadStatus{};
    VPADReadError vpadError;
    bool vpadValid = false;

    if (VPADRead(VPAD_CHAN_0, &vpadStatus, 1, &vpadError) > 0 &&
        vpadError == VPAD_READ_SUCCESS) {
        VPADTouchData calibrated{};
        VPADGetTPCalibratedPoint(VPAD_CHAN_0, &calibrated, &vpadStatus.tpFiltered1);
        vpadStatus.tpNormal    = calibrated;
        vpadStatus.tpFiltered1 = calibrated;
        vpadStatus.tpFiltered2 = calibrated;
        vpadValid = true;
    }

    KPADStatus kpadStatus[4]{};
    for (int i = 0; i < 4; ++i) {
        KPADError kpadError;
        if (KPADReadEx(static_cast<KPADChan>(i), &kpadStatus[i], 1, &kpadError) > 0 &&
            kpadError == KPAD_ERROR_OK) {
            switch (kpadStatus[i].extensionType) {
            case WPAD_EXT_PRO_CONTROLLER:
                vpadStatus.hold    |= MapProToVPADButtons(kpadStatus[i].pro.hold);
                vpadStatus.trigger |= MapProToVPADButtons(kpadStatus[i].pro.trigger);
                vpadStatus.release |= MapProToVPADButtons(kpadStatus[i].pro.release);
                vpadValid = true;
                break;
            case WPAD_EXT_CLASSIC:
            case WPAD_EXT_MPLUS_CLASSIC:
                vpadStatus.hold    |= MapWiimoteToVPADButtons(kpadStatus[i].hold)
                                    | MapClassicToVPADButtons(kpadStatus[i].classic.hold);
                vpadStatus.trigger |= MapWiimoteToVPADButtons(kpadStatus[i].trigger)
                                    | MapClassicToVPADButtons(kpadStatus[i].classic.trigger);
                vpadStatus.release |= MapWiimoteToVPADButtons(kpadStatus[i].release)
                                    | MapClassicToVPADButtons(kpadStatus[i].classic.release);
                vpadValid = true;
                break;
            case WPAD_EXT_NUNCHUK:
            case WPAD_EXT_MPLUS_NUNCHUK:
                vpadStatus.hold    |= MapWiimoteToVPADButtons(kpadStatus[i].hold)
                                    | MapNunchukToVPADButtons(kpadStatus[i].nunchuk.hold)
                                    | NunchukStickToVPADButtons(kpadStatus[i].nunchuk.stick);
                vpadStatus.trigger |= MapWiimoteToVPADButtons(kpadStatus[i].trigger)
                                    | MapNunchukToVPADButtons(kpadStatus[i].nunchuk.trigger);
                vpadStatus.release |= MapWiimoteToVPADButtons(kpadStatus[i].release)
                                    | MapNunchukToVPADButtons(kpadStatus[i].nunchuk.release);
                vpadValid = true;
                break;
            case WPAD_EXT_CORE:
            case WPAD_EXT_MPLUS:
                vpadStatus.hold    |= MapWiimoteToVPADButtons(kpadStatus[i].hold);
                vpadStatus.trigger |= MapWiimoteToVPADButtons(kpadStatus[i].trigger);
                vpadStatus.release |= MapWiimoteToVPADButtons(kpadStatus[i].release);
                vpadValid = true;
                break;
            }
        }
    }

    if (vpadValid) {
        SDL_WiiUSetSWKBDVPAD(&vpadStatus);
    }

    SDL_PumpEvents();

    if (sState == State::Disappearing) {
        if (nn::swkbd::GetStateInputForm() == nn::swkbd::State::Hidden) {
            sState = State::Idle;
            if (sCallback) {
                sCallback(sPendingConfirmed, sInputBuffer);
                sCallback = nullptr;
            }
        }
    }
}

void Keyboard::Draw() {
}

Keyboard::State Keyboard::GetState() { return sState; }
bool            Keyboard::IsActive() { return sState != State::Idle; }

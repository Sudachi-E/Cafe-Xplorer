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
    if (VPADRead(VPAD_CHAN_0, &vpadStatus, 1, &vpadError) > 0 &&
        vpadError == VPAD_READ_SUCCESS) {
        VPADTouchData calibrated{};
        VPADGetTPCalibratedPoint(VPAD_CHAN_0, &calibrated, &vpadStatus.tpFiltered1);
        vpadStatus.tpNormal    = calibrated;
        vpadStatus.tpFiltered1 = calibrated;
        vpadStatus.tpFiltered2 = calibrated;
        SDL_WiiUSetSWKBDVPAD(&vpadStatus);
    }

    KPADStatus kpadStatus{};
    for (int i = 0; i < 4; ++i) {
        if (KPADReadEx(static_cast<KPADChan>(i), &kpadStatus, 1, nullptr) > 0) {
            SDL_WiiUSetSWKBDKPAD(i, &kpadStatus);
        }
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

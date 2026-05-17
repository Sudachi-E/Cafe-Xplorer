#pragma once

#include <functional>
#include <string>
#include <SDL_system.h>

class Keyboard {
public:
    enum class State {
        Idle,
        Visible,
        Disappearing,
    };

    static bool Init();
    static void Shutdown();

    static bool RequestKeyboard(const std::string& initialText,
                                const std::string& hint,
                                std::function<void(bool, const std::string&)> callback,
                                SDL_WiiUSWKBDKeyboardMode mode = SDL_WIIU_SWKBD_KEYBOARD_MODE_RESTRICTED);

    static void Update();

    static void Draw();

    static State GetState();
    static bool  IsActive();

    static State       sState;
    static std::string sInputBuffer;
    static bool        sPendingConfirmed;
    static std::function<void(bool, const std::string&)> sCallback;
};

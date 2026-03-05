#pragma once

#include <string>
#include <functional>

class Keyboard {
public:
    enum class State {
        Idle,
        Appearing,
        Visible,
        Disappearing
    };

    static bool Init();
    static void Shutdown();
    
    // New async API
    static bool RequestKeyboard(const std::string& initialText, const std::string& hint, 
                                std::function<void(bool, const std::string&)> callback);
    static void Update();
    static void Draw();
    static State GetState();
    static bool IsActive();

private:
    static State sState;
    static std::string sInputBuffer;
    static std::function<void(bool, const std::string&)> sCallback;
};

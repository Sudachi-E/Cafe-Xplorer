#pragma once

#include <cstdint>
#include <functional>
#include <string>

class CustomKeyboard {
public:
    static void Open(const std::string& initialText,
                     const std::string& hint,
                     bool restricted,
                     std::function<void(bool, const std::string&)> callback);

    static void Close();
    static bool IsOpen();

    static void Update(uint32_t buttonsHeld, uint32_t buttonsPressed);
    static void Draw();

    static const std::string& GetText();
};
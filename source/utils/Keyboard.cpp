#include "Keyboard.hpp"
#include <nn/swkbd.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <padscore/kpad.h>
#include <whb/log.h>
#include <cstring>

static bool sKeyboardInitialized = false;
static void* sWorkMemory = nullptr;
static FSClient* sFsClient = nullptr;
static std::u16string sInitialTextU16;
static std::u16string sHintTextU16;

Keyboard::State Keyboard::sState = Keyboard::State::Idle;
std::string Keyboard::sInputBuffer;
std::function<void(bool, const std::string&)> Keyboard::sCallback;

bool Keyboard::Init() {
    if (sKeyboardInitialized) {
        return true;
    }
    
    // Allocate work memory for keyboard (needs about 400KB)
    sWorkMemory = MEMAllocFromDefaultHeap(0x100000); // 1MB to be safe
    if (!sWorkMemory) {
        return false;
    }
    
    // Allocate FSClient
    sFsClient = (FSClient*)MEMAllocFromDefaultHeap(sizeof(FSClient));
    if (!sFsClient) {
        MEMFreeToDefaultHeap(sWorkMemory);
        sWorkMemory = nullptr;
        return false;
    }
    
    FSAddClient(sFsClient, FS_ERROR_FLAG_NONE);
    
    // Create keyboard
    nn::swkbd::CreateArg createArg;
    createArg.workMemory = sWorkMemory;
    createArg.regionType = nn::swkbd::RegionType::Europe;
    createArg.unk_0x08 = 0;
    createArg.fsClient = sFsClient;
    
    nn::swkbd::Create(createArg);
    
    sKeyboardInitialized = true;
    sState = State::Idle;
    return true;
}

void Keyboard::Shutdown() {
    if (!sKeyboardInitialized) {
        return;
    }
    
    if (sFsClient) {
        FSDelClient(sFsClient, FS_ERROR_FLAG_NONE);
        MEMFreeToDefaultHeap(sFsClient);
        sFsClient = nullptr;
    }
    
    // Free work memory
    if (sWorkMemory) {
        MEMFreeToDefaultHeap(sWorkMemory);
        sWorkMemory = nullptr;
    }
    
    sKeyboardInitialized = false;
    sState = State::Idle;
}

bool Keyboard::RequestKeyboard(const std::string& initialText, const std::string& hint, 
                                std::function<void(bool, const std::string&)> callback) {
    if (!sKeyboardInitialized || sState != State::Idle) {
        return false;
    }
    
    sCallback = callback;
    sInputBuffer = initialText;
    
    // Convert UTF-8 to UTF-16 and store in static variables
    sInitialTextU16.clear();
    for (char c : initialText) {
        sInitialTextU16 += static_cast<char16_t>(static_cast<unsigned char>(c));
    }
    
    sHintTextU16.clear();
    for (char c : hint) {
        sHintTextU16 += static_cast<char16_t>(static_cast<unsigned char>(c));
    }
    
    // Configure keyboard appearance
    nn::swkbd::AppearArg appearArg;
    
    // Set initial text if provided
    if (!initialText.empty()) {
        appearArg.inputFormArg.initialText = sInitialTextU16.c_str();
    }
    
    // Set hint text if provided
    if (!hint.empty()) {
        appearArg.inputFormArg.hintText = sHintTextU16.c_str();
    }
    
    // Show keyboard
    if (!nn::swkbd::AppearInputForm(appearArg)) {
        sCallback = nullptr;
        return false;
    }
    
    sState = State::Appearing;
    return true;
}

void Keyboard::Update() {
    if (!sKeyboardInitialized || sState == State::Idle) {
        return;
    }
    
    // Read controller input for keyboard
    nn::swkbd::ControllerInfo controllerInfo;
    
    // Read VPad (GamePad)
    VPADStatus vpadStatus;
    VPADReadError vpadError;
    if (VPADRead(VPAD_CHAN_0, &vpadStatus, 1, &vpadError) > 0 && vpadError == VPAD_READ_SUCCESS) {
        // Calibrate touch data before passing to keyboard
        VPADTouchData calibratedTouch;
        VPADGetTPCalibratedPoint(VPAD_CHAN_0, &calibratedTouch, &vpadStatus.tpFiltered1);
        
        // Replace the touch data with calibrated version
        vpadStatus.tpNormal = calibratedTouch;
        vpadStatus.tpFiltered1 = calibratedTouch;
        vpadStatus.tpFiltered2 = calibratedTouch;
        
        controllerInfo.vpad = &vpadStatus;
    }
    
    // Read KPad (Wiimotes)
    KPADStatus kpadStatus[4];
    for (int i = 0; i < 4; i++) {
        int32_t result = KPADReadEx((KPADChan)i, &kpadStatus[i], 1, nullptr);
        if (result > 0) {
            controllerInfo.kpad[i] = &kpadStatus[i];
        }
    }
    
    nn::swkbd::Calc(controllerInfo);
    
    if (sState == State::Appearing) {
        if (nn::swkbd::IsNeedCalcSubThreadFont()) {
            nn::swkbd::CalcSubThreadFont();
        }
        
        if (nn::swkbd::IsNeedCalcSubThreadPredict()) {
            nn::swkbd::CalcSubThreadPredict();
        }
        
        if (nn::swkbd::IsDecideOkButton(nullptr) || nn::swkbd::IsDecideCancelButton(nullptr)) {
            sState = State::Disappearing;
        } else if (nn::swkbd::GetStateInputForm() == nn::swkbd::State::Visible) {
            sState = State::Visible;
        }
    } else if (sState == State::Visible) {
        if (nn::swkbd::IsNeedCalcSubThreadFont()) {
            nn::swkbd::CalcSubThreadFont();
        }
        
        if (nn::swkbd::IsNeedCalcSubThreadPredict()) {
            nn::swkbd::CalcSubThreadPredict();
        }
        
        bool okPressed = nn::swkbd::IsDecideOkButton(nullptr);
        bool cancelPressed = nn::swkbd::IsDecideCancelButton(nullptr);
        
        if (okPressed || cancelPressed) {
            if (okPressed) {
                // Get the input text
                const char16_t* inputStr = nn::swkbd::GetInputFormString();
                if (inputStr) {
                    // Convert UTF-16 to UTF-8
                    sInputBuffer.clear();
                    for (int i = 0; inputStr[i] != 0; i++) {
                        if (inputStr[i] < 0x80) {
                            sInputBuffer += static_cast<char>(inputStr[i]);
                        } else if (inputStr[i] < 0x800) {
                            sInputBuffer += static_cast<char>(0xC0 | (inputStr[i] >> 6));
                            sInputBuffer += static_cast<char>(0x80 | (inputStr[i] & 0x3F));
                        } else {
                            sInputBuffer += static_cast<char>(0xE0 | (inputStr[i] >> 12));
                            sInputBuffer += static_cast<char>(0x80 | ((inputStr[i] >> 6) & 0x3F));
                            sInputBuffer += static_cast<char>(0x80 | (inputStr[i] & 0x3F));
                        }
                    }
                }
            }
            
            nn::swkbd::DisappearInputForm();
            sState = State::Disappearing;
            
            // Call callback
            if (sCallback) {
                sCallback(okPressed, sInputBuffer);
                sCallback = nullptr;
            }
        }
    } else if (sState == State::Disappearing) {
        if (nn::swkbd::GetStateInputForm() == nn::swkbd::State::Hidden) {
            sState = State::Idle;
        }
    }
}

void Keyboard::Draw() {
    if (!sKeyboardInitialized || sState == State::Idle) {
        return;
    }
    
    nn::swkbd::DrawDRC();
    nn::swkbd::DrawTV();
}

Keyboard::State Keyboard::GetState() {
    return sState;
}

bool Keyboard::IsActive() {
    return sState != State::Idle;
}

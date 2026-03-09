#include "Gfx.hpp"
#include "input/CombinedInput.h"
#include "input/VPADInput.h"
#include "input/WPADInput.h"
#include "screens/FileManagerScreen.hpp"
#include "utils/Keyboard.hpp"
#include "utils/logger.h"
#include "utils/Settings.hpp"
#include "utils/FilesystemManager.hpp"
#include "filemanager/PathConverter.hpp"
#include <coreinit/title.h>
#include <memory>
#include <padscore/kpad.h>
#include <sndcore2/core.h>
#include <sysapp/launch.h>
#include <whb/proc.h>
#include <whb/log.h>

inline bool RunningFromMiiMaker() {
    return (OSGetTitleID() & 0xFFFFFFFFFFFFF0FFull) == 0x000500101004A000ull;
}

int main(int argc, char const *argv[]) {
    WHBProcInit();
    
    initLogging();
    WHBLogPrintf("WiiUXplorer starting... (UDP logging enabled)");

    Settings::Initialize();
    
    PathConverter::AddRootDirectory("fs");
    
         // mount additional filesystems if full access is enabled
    if (Settings::GetFullFilesystemAccess()) {
        WHBLogPrintf("Full filesystem access enabled, mounting all storage");
        FilesystemManager::MountAllFilesystems();
    } else {
        WHBLogPrintf("Full filesystem access disabled, SD card only mode");
    }

    AXInit();
    AXQuit();

    KPADInit();
    WPADEnableURCC(TRUE);

    if (!Gfx::Init()) {
        WHBLogPrintf("Failed to initialize graphics");
        WHBProcShutdown();
        return -1;
    }
    
    WHBLogPrintf("Graphics initialized");

    Keyboard::Init();
    WHBLogPrintf("Keyboard initialized");

    std::unique_ptr<Screen> fileManagerScreen = std::make_unique<FileManagerScreen>();
    WHBLogPrintf("FileManagerScreen created");

    CombinedInput baseInput;
    VPadInput vpadInput;
    WPADInput wpadInputs[4] = {
        WPADInput(WPAD_CHAN_0),
        WPADInput(WPAD_CHAN_1),
        WPADInput(WPAD_CHAN_2),
        WPADInput(WPAD_CHAN_3)
    };

    while (WHBProcIsRunning()) {
        Keyboard::Update();
        
        if (!Keyboard::IsActive()) {
            baseInput.reset();
            
            if (vpadInput.update(1280, 720)) {
                baseInput.combine(vpadInput);
            }
            
            for (auto &wpadInput : wpadInputs) {
                if (wpadInput.update(1280, 720)) {
                    baseInput.combine(wpadInput);
                }
            }
            
            baseInput.process();

            if (!fileManagerScreen->Update(baseInput)) {
                if (RunningFromMiiMaker()) {
                    break;
                } else {
                    SYSLaunchMenu();
                }
            }
        }

        fileManagerScreen->Draw();
        
        Keyboard::Draw();
        
        Gfx::Render();
    }

    fileManagerScreen.reset();
    Keyboard::Shutdown();
    Gfx::Shutdown();
    
    if (FilesystemManager::IsMochaInitialized()) {
        FilesystemManager::UnmountAllFilesystems();
        WHBLogPrintf("Filesystems unmounted");
    }
    
    WHBProcShutdown();
    deinitLogging();
    
    return 0;
}

#include "Gfx.hpp"
#include "Screen.hpp"
#include "input/CombinedInput.h"
#include "input/VPADInput.h"
#include "input/WPADInput.h"
#include "screens/FileManagerScreen.hpp"
#include "utils/Keyboard.hpp"
#include "utils/logger.h"
#include "utils/Settings.hpp"
#include "utils/FilesystemManager.hpp"
#include "utils/FtpServer.hpp"
#include "filemanager/PathConverter.hpp"
#include <coreinit/title.h>
#include <memory>
#include <padscore/kpad.h>
#include <sndcore2/core.h>
#include <sysapp/launch.h>
#include <vpad/input.h>
#include <whb/proc.h>
#include <whb/log.h>

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

    VPADInit();
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

    while (true) {
        if (!WHBProcIsRunning()) break;

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

            {
                Input::eControllerType activeType = Screen::GetActiveControllerType();

                bool wpadActive = false;
                for (auto &wpadInput : wpadInputs) {
                    if (wpadInput.isConnected() &&
                        (wpadInput.data.buttons_h != 0 || wpadInput.data.buttons_d != 0)) {
                        activeType = wpadInput.getControllerType();
                        wpadActive = true;
                        break;
                    }
                }

                if (!wpadActive &&
                    (vpadInput.data.buttons_h != 0 || vpadInput.data.buttons_d != 0)) {
                    activeType = Input::CONTROLLER_TYPE_GAMEPAD;
                }

                Screen::SetActiveControllerType(activeType);
            }

            if (!fileManagerScreen->Update(baseInput)) {
                    SYSLaunchMenu();
                    break;
                }
            }

        if (!WHBProcIsRunning()) break;

        fileManagerScreen->Draw();
        Keyboard::Draw();

        if (!WHBProcIsRunning()) break;

        Gfx::Render();
    }

    WHBLogPrintf("Starting cleanup sequence...");
    
    fileManagerScreen.reset();
    WHBLogPrintf("FileManagerScreen destroyed");
    
    Keyboard::Shutdown();
    WHBLogPrintf("Keyboard shutdown complete");
    
    Gfx::Shutdown();
    WHBLogPrintf("Graphics shutdown complete");
    
    KPADShutdown();
    VPADShutdown();
    WHBLogPrintf("KPAD/VPAD shutdown complete");
    
    FtpServer::Stop();
    WHBLogPrintf("FTP server stopped");
    
    if (FilesystemManager::IsMochaInitialized()) {
        FilesystemManager::UnmountAllFilesystems();
        WHBLogPrintf("Filesystems unmounted");
        FilesystemManager::Shutdown();
        WHBLogPrintf("FilesystemManager shutdown complete");
    }
    
    WHBProcShutdown();
    WHBLogPrintf("WHBProc shutdown complete");
    
    deinitLogging();
    
    return 0;
}

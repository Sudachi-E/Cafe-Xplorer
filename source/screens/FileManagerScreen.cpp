#include "FileManagerScreen.hpp"
#include "TextEditorScreen.hpp"
#include "ImageViewerScreen.hpp"
#include "VideoPlayerScreen.hpp"
#include "AudioPlayerScreen.hpp"
#include "SettingsScreen.hpp"
#include "../Gfx.hpp"
#include "../utils/Keyboard.hpp"
#include "../utils/Settings.hpp"
#include "../utils/FilesystemManager.hpp"
#include "../filemanager/PathConverter.hpp"
#include <whb/log.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <sysapp/launch.h>
#include <rpxloader/rpxloader.h>

FileManagerScreen::FileManagerScreen() : mSelectedIndex(0), mScrollOffset(0), mShowContextMenu(false), mContextMenuSelection(0), mClipboardIsDirectory(false), mClipboardIsMove(false), mShowDeletionModal(false), mShowLoadingModal(false), mLoadingStartTime(0), mShowLaunchConfirmModal(false), mLaunchModalSelection(0), mLastAnalogScrollTime(0) {
    Settings::Initialize();
    
    if (Settings::GetFullFilesystemAccess()) {
        mFileManager.ScanDirectory("/");
    } else {
        mFileManager.ScanDirectory("/fs/vol/external01");
    }
}

FileManagerScreen::~FileManagerScreen() = default;

void FileManagerScreen::Draw() {
    if (mSettingsScreen) {
        mSettingsScreen->Draw();
        return;
    }
    
    if (mTextEditor) {
        mTextEditor->Draw();
        return;
    }
    
    if (mImageViewer) {
        mImageViewer->Draw();
        return;
    }
    
    if (mVideoPlayer) {
        mVideoPlayer->Draw();
        return;
    }
    
    if (mAudioPlayer) {
        mAudioPlayer->Draw();
        return;
    }
    
    Gfx::Clear(Gfx::COLOR_BACKGROUND);
    
    DrawTopBar(mFileManager.GetCurrentPath().c_str());
    
    std::string leftText = "A: Select  X: Menu  Y: Settings";
    if (mFileManager.HasMoreEntries()) {
        std::ostringstream oss;
        oss << "Loaded " << mFileManager.GetEntries().size() << "/" << mFileManager.GetTotalEntryCount();
        DrawBottomBar(leftText.c_str(), oss.str().c_str(), "B: Back  HOME: Exit");
    } else {
        DrawBottomBar(leftText.c_str(), "B: Back", "HOME: Exit");
    }
    
    const auto& entries = mFileManager.GetEntries();
    
    if (entries.empty() && !mFileManager.HasMoreEntries()) {
        Gfx::DrawRectFilled(Gfx::SCREEN_WIDTH / 2 - 200, Gfx::SCREEN_HEIGHT / 2 - 50, 400, 100, Gfx::COLOR_ALT_BACKGROUND);
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 48, 
                   Gfx::COLOR_ALT_TEXT, "Empty directory", Gfx::ALIGN_CENTER);
    } else {
        int y = 80;
        int itemHeight = 60;
        int visibleItems = (Gfx::SCREEN_HEIGHT - 160) / itemHeight;
        
        if (mSelectedIndex < mScrollOffset) {
            mScrollOffset = mSelectedIndex;
        } else if (mSelectedIndex >= mScrollOffset + visibleItems) {
            mScrollOffset = mSelectedIndex - visibleItems + 1;
        }
        
        for (size_t i = mScrollOffset; i < entries.size() && i < mScrollOffset + visibleItems; i++) {
            bool isSelected = (i == mSelectedIndex);
            
            if (isSelected) {
                Gfx::DrawRectFilled(0, y - 5, Gfx::SCREEN_WIDTH, itemHeight, Gfx::COLOR_HIGHLIGHTED);
            } else {
                // Draw subtle background for non-selected items
                Gfx::DrawRectFilled(0, y - 5, Gfx::SCREEN_WIDTH, itemHeight, Gfx::COLOR_ALT_BACKGROUND);
            }
            
            std::string displayName = entries[i].isDirectory ? "[DIR] " : "      ";
            displayName += entries[i].name;
            
            Gfx::Print(40, y + 20, 36, isSelected ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT, 
                       displayName, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
            
            if (!entries[i].isDirectory) {
                std::string sizeStr = FormatSize(entries[i].size);
                Gfx::Print(Gfx::SCREEN_WIDTH - 60, y + 20, 32, 
                           isSelected ? Gfx::COLOR_WHITE : Gfx::COLOR_ALT_TEXT, 
                           sizeStr, Gfx::ALIGN_RIGHT | Gfx::ALIGN_VERTICAL);
            }
            
            y += itemHeight;
        }
        
        if (mFileManager.HasMoreEntries() && mScrollOffset + visibleItems >= entries.size()) {
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, y + 20, 36, 
                       Gfx::COLOR_ALT_TEXT, "Loading more...", Gfx::ALIGN_CENTER);
        }
    }
    
    if (mShowContextMenu) {
        DrawContextMenu();
    }
    
    if (mShowDeletionModal) {
        DrawDeletionModal();
    }
    
    if (mShowLoadingModal) {
        DrawLoadingModal();
    }
    
    if (mShowLaunchConfirmModal) {
        DrawLaunchConfirmModal();
    }
}

bool FileManagerScreen::Update(Input &input) {
    if (mShowLaunchConfirmModal) {
        if (input.data.buttons_d & Input::BUTTON_LEFT || input.data.buttons_d & Input::BUTTON_RIGHT) {
            mLaunchModalSelection = 1 - mLaunchModalSelection;
        }
        
        if (input.data.buttons_d & Input::BUTTON_A) {
            if (mLaunchModalSelection == 0) {
                LaunchHomebrew(mLaunchFilePath);
            }
            mShowLaunchConfirmModal = false;
            mLaunchModalSelection = 0;
        }
        
        if (input.data.buttons_d & Input::BUTTON_B) {
            mShowLaunchConfirmModal = false;
            mLaunchModalSelection = 0;
        }
        
        return true;
    }
    
    if (mSettingsScreen) {
        if (!mSettingsScreen->Update(input) || mSettingsScreen->ShouldClose()) {
            if (mSettingsScreen->SettingsChanged()) {
                PathConverter::ClearRootDirectory();
                PathConverter::AddRootDirectory("fs");
                
                if (Settings::GetFullFilesystemAccess()) {
                    WHBLogPrintf("Mounting filesystems after settings change");
                    FilesystemManager::MountAllFilesystems();
                    mFileManager.ScanDirectory("/");
                } else {
                    WHBLogPrintf("Unmounting filesystems after settings change");
                    FilesystemManager::UnmountAllFilesystems();
                    mFileManager.ScanDirectory("/fs/vol/external01");
                }
                mSelectedIndex = 0;
                mScrollOffset = 0;
            }
            mSettingsScreen.reset();
        }
        return true;
    }
    
    if (mTextEditor) {
        if (!mTextEditor->Update(input) || mTextEditor->ShouldClose()) {
            mTextEditor.reset();
            mFileManager.ScanDirectory(mFileManager.GetCurrentPath());
        }
        return true;
    }
    
    if (mImageViewer) {
        if (!mImageViewer->Update(input) || mImageViewer->ShouldClose()) {
            mImageViewer.reset();
        }
        return true;
    }
    
    if (mVideoPlayer) {
        if (!mVideoPlayer->Update(input) || mVideoPlayer->ShouldClose()) {
            mVideoPlayer.reset();
        }
        return true;
    }
    
    if (mAudioPlayer) {
        if (!mAudioPlayer->Update(input) || mAudioPlayer->ShouldClose()) {
            mAudioPlayer.reset();
        }
        return true;
    }
    
    if (input.data.buttons_d & Input::BUTTON_Y) {
        mSettingsScreen = std::make_unique<SettingsScreen>();
        return true;
    }
    
    if (input.data.buttons_d & Input::BUTTON_X) {
        mShowContextMenu = !mShowContextMenu;
        mContextMenuSelection = 0;
        return true;
    }
    
    if (mShowContextMenu) {
        if (input.data.buttons_d & Input::BUTTON_DOWN) {
            mContextMenuSelection = (mContextMenuSelection + 1) % 7;
        }
        if (input.data.buttons_d & Input::BUTTON_UP) {
            mContextMenuSelection = (mContextMenuSelection - 1 + 7) % 7;
        }
        
        if (input.data.buttons_d & Input::BUTTON_A) {
            mShowContextMenu = false;
            
            if (mContextMenuSelection == 0) {
                Keyboard::RequestKeyboard("", "Enter filename", [this](bool confirmed, const std::string& text) {
                    if (confirmed && !text.empty()) {
                        CreateNewFile(text);
                    }
                });
            } else if (mContextMenuSelection == 1) {
                Keyboard::RequestKeyboard("", "Enter folder name", [this](bool confirmed, const std::string& text) {
                    if (confirmed && !text.empty()) {
                        CreateNewFolder(text);
                    }
                });
            } else if (mContextMenuSelection == 2) {
                const auto& entries = mFileManager.GetEntries();
                if (!entries.empty() && mSelectedIndex < entries.size()) {
                    const auto& entry = entries[mSelectedIndex];
                    mClipboardPath = entry.path;
                    mClipboardIsDirectory = entry.isDirectory;
                    mClipboardIsMove = false;
                }
            } else if (mContextMenuSelection == 3) {
                const auto& entries = mFileManager.GetEntries();
                if (!entries.empty() && mSelectedIndex < entries.size()) {
                    const auto& entry = entries[mSelectedIndex];
                    mClipboardPath = entry.path;
                    mClipboardIsDirectory = entry.isDirectory;
                    mClipboardIsMove = true;
                }
            } else if (mContextMenuSelection == 4) {
                if (!mClipboardPath.empty()) {
                    bool success = false;
                    if (mClipboardIsMove) {
                        success = mFileManager.MoveEntry(mClipboardPath, mFileManager.GetCurrentPath(), mClipboardIsDirectory);
                    } else {
                        success = mFileManager.PasteEntry(mClipboardPath, mFileManager.GetCurrentPath(), mClipboardIsDirectory);
                    }
                    
                    if (success) {
                        mClipboardPath.clear();
                        mClipboardIsDirectory = false;
                        mClipboardIsMove = false;
                        mFileManager.ScanDirectory(mFileManager.GetCurrentPath());
                    }
                }
            } else if (mContextMenuSelection == 5) {
                const auto& entries = mFileManager.GetEntries();
                if (!entries.empty() && mSelectedIndex < entries.size()) {
                    const auto& entry = entries[mSelectedIndex];
                    Keyboard::RequestKeyboard(entry.name, "Enter new name", [this, entry](bool confirmed, const std::string& text) {
                        if (confirmed && !text.empty()) {
                            if (mFileManager.RenameEntry(entry.path, text)) {
                                mFileManager.ScanDirectory(mFileManager.GetCurrentPath());
                            }
                        }
                    });
                }
            } else if (mContextMenuSelection == 6) {
                const auto& entries = mFileManager.GetEntries();
                if (!entries.empty() && mSelectedIndex < entries.size()) {
                    const auto& entry = entries[mSelectedIndex];
                    
                    mDeletionFileName = entry.name;
                    mShowDeletionModal = true;
                    
                    Draw();
                    Gfx::Render();
                    
                    if (mFileManager.DeleteEntry(entry.path, entry.isDirectory)) {
                        mFileManager.ScanDirectory(mFileManager.GetCurrentPath());
                        const auto& newEntries = mFileManager.GetEntries();
                        if (mSelectedIndex >= newEntries.size() && !newEntries.empty()) {
                            mSelectedIndex = newEntries.size() - 1;
                        }
                    }
                    
                    mShowDeletionModal = false;
                }
            }
        }
        
        if (input.data.buttons_d & Input::BUTTON_B) {
            mShowContextMenu = false;
        }
        return true;
    }
    
    const auto& entries = mFileManager.GetEntries();
    
    // Handle D-pad navigation
    if (input.data.buttons_d & Input::BUTTON_DOWN) {
        if (!entries.empty()) {
            mSelectedIndex = (mSelectedIndex + 1) % entries.size();
            
            if (mFileManager.HasMoreEntries() && mSelectedIndex >= entries.size() - 10) {
                mFileManager.LoadMoreEntries();
            }
        }
    }
    
    if (input.data.buttons_d & Input::BUTTON_UP) {
        if (!entries.empty()) {
            mSelectedIndex = (mSelectedIndex == 0) ? entries.size() - 1 : mSelectedIndex - 1;
        }
    }
    
    // Handle left analog stick for faster navigation
    if (!entries.empty()) {
        const float STICK_THRESHOLD = 0.5f;
        const int FAST_SCROLL_AMOUNT = 5;
        const uint64_t ANALOG_SCROLL_DELAY_MS = 150; // Delay between analog scroll movements
        
        uint64_t currentTime = OSGetSystemTime();
        uint64_t timeSinceLastScroll = OSTicksToMilliseconds(currentTime - mLastAnalogScrollTime);
        
        if (timeSinceLastScroll >= ANALOG_SCROLL_DELAY_MS) {
            if (input.data.leftStickY < -STICK_THRESHOLD) {
                // Stick pushed down - move down faster
                mSelectedIndex = std::min(mSelectedIndex + FAST_SCROLL_AMOUNT, entries.size() - 1);
                mLastAnalogScrollTime = currentTime;
                
                if (mFileManager.HasMoreEntries() && mSelectedIndex >= entries.size() - 10) {
                    mFileManager.LoadMoreEntries();
                }
            } else if (input.data.leftStickY > STICK_THRESHOLD) {
                // Stick pushed up - move up faster
                if (mSelectedIndex >= FAST_SCROLL_AMOUNT) {
                    mSelectedIndex -= FAST_SCROLL_AMOUNT;
                } else {
                    mSelectedIndex = 0;
                }
                mLastAnalogScrollTime = currentTime;
            }
        }
    }
    
    if (input.data.buttons_d & Input::BUTTON_A) {
        if (!entries.empty() && mSelectedIndex < entries.size()) {
            const auto& entry = entries[mSelectedIndex];
            if (entry.isDirectory) {
                mLastVisitedDir = entry.name;
                std::string dirPath = entry.path;
                
                if (ScanDirectoryWithModal(dirPath)) {
                    mSelectedIndex = 0;
                    mScrollOffset = 0;
                }
            } else {
                if (IsTextFile(entry.name)) {
                    mTextEditor = std::make_unique<TextEditorScreen>(entry.path);
                }
                else if (IsImageFile(entry.name)) {
                    mImageViewer = std::make_unique<ImageViewerScreen>(entry.path);
                }
                else if (IsVideoFile(entry.name)) {
                    WHBLogPrintf("===========================================");
                    WHBLogPrintf("VIDEO FILE SELECTED");
                    WHBLogPrintf("===========================================");
                    WHBLogPrintf("File name: %s", entry.name.c_str());
                    WHBLogPrintf("Full path: %s", entry.path.c_str());
                    WHBLogPrintf("File size: %u bytes", (unsigned int)entry.size);
                    WHBLogPrintf("Creating VideoPlayerScreen...");
                    mVideoPlayer = std::make_unique<VideoPlayerScreen>(entry.path);
                    WHBLogPrintf("VideoPlayerScreen created");
                    WHBLogPrintf("===========================================");
                }
                else if (IsAudioFile(entry.name)) {
                    mAudioPlayer = std::make_unique<AudioPlayerScreen>(entry.path);
                }
                else if (IsRPXFile(entry.name) || IsWUHBFile(entry.name)) {
                    mLaunchFileName = entry.name;
                    mLaunchFilePath = entry.path;
                    mShowLaunchConfirmModal = true;
                    mLaunchModalSelection = 0;
                }
            }
        }
    }
    
    if (input.data.buttons_d & Input::BUTTON_B) {
        std::string currentPath = mFileManager.GetCurrentPath();
        if (currentPath != "/" && !currentPath.empty()) {
            mLoadingPath = "..";
            mLoadingStartTime = OSGetSystemTime();
            mShowLoadingModal = false;
            
            mFileManager.SetProgressCallback([this]() {
                uint64_t currentTime = OSGetSystemTime();
                uint64_t elapsedMs = OSTicksToMilliseconds(currentTime - mLoadingStartTime);
                
                if (elapsedMs >= 1500 && !mShowLoadingModal) {
                    mShowLoadingModal = true;
                    Draw();
                    Gfx::Render();
                }
            });
            
            if (mFileManager.NavigateUp()) {
                if (!mLastVisitedDir.empty()) {
                    const auto& newEntries = mFileManager.GetEntries();
                    for (size_t i = 0; i < newEntries.size(); i++) {
                        if (newEntries[i].name == mLastVisitedDir && newEntries[i].isDirectory) {
                            mSelectedIndex = i;
                            mScrollOffset = 0;
                            mLastVisitedDir.clear();
                            mFileManager.SetProgressCallback(nullptr);
                            mShowLoadingModal = false;
                            return true;
                        }
                    }
                    mLastVisitedDir.clear();
                }
                mSelectedIndex = 0;
                mScrollOffset = 0;
            }
            
            mFileManager.SetProgressCallback(nullptr);
            mShowLoadingModal = false;
        }
    }
    
    return true;
}

std::string FileManagerScreen::FormatSize(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unitIndex < 3) {
        size /= 1024.0;
        unitIndex++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
    return oss.str();
}

bool FileManagerScreen::IsTextFile(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Check for text file extensions
    return lower.ends_with(".txt") || 
           lower.ends_with(".json") || 
           lower.ends_with(".log") ||
           lower.ends_with(".ini") ||
           lower.ends_with(".cfg") ||
           lower.ends_with(".xml") ||
           lower.ends_with(".md");
}

bool FileManagerScreen::IsImageFile(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    return lower.ends_with(".png") || lower.ends_with(".jpg") || lower.ends_with(".jpeg");
}

bool FileManagerScreen::IsVideoFile(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    return lower.ends_with(".mp4") || 
           lower.ends_with(".avi") || 
           lower.ends_with(".mkv") ||
           lower.ends_with(".mov");
}

bool FileManagerScreen::IsAudioFile(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    return lower.ends_with(".mp3");
}

bool FileManagerScreen::IsRPXFile(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    return lower.ends_with(".rpx");
}

bool FileManagerScreen::IsWUHBFile(const std::string& filename) {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    return lower.ends_with(".wuhb");
}

void FileManagerScreen::DrawContextMenu() {
    int menuWidth = 400;
    int menuHeight = 520;
    int menuX = Gfx::SCREEN_WIDTH - menuWidth - 50;
    int menuY = (Gfx::SCREEN_HEIGHT - menuHeight) / 2;
    int borderWidth = 3;
    
    Gfx::DrawRectFilled(menuX, menuY, menuWidth, menuHeight, Gfx::COLOR_ALT_BACKGROUND);
    Gfx::DrawRectFilled(menuX, menuY, menuWidth, borderWidth, Gfx::COLOR_HIGHLIGHTED); // Top
    Gfx::DrawRectFilled(menuX, menuY + menuHeight - borderWidth, menuWidth, borderWidth, Gfx::COLOR_HIGHLIGHTED); // Bottom
    Gfx::DrawRectFilled(menuX, menuY, borderWidth, menuHeight, Gfx::COLOR_HIGHLIGHTED); // Left
    Gfx::DrawRectFilled(menuX + menuWidth - borderWidth, menuY, borderWidth, menuHeight, Gfx::COLOR_HIGHLIGHTED); // Right
    
    int optionY = menuY + 40;
    int optionSpacing = 60;
    
    bool newFileSelected = (mContextMenuSelection == 0);
    if (newFileSelected) {
        Gfx::DrawRectFilled(menuX + 10, optionY - 5, menuWidth - 20, 50, Gfx::COLOR_HIGHLIGHTED);
    }
    Gfx::Print(menuX + menuWidth / 2, optionY + 20, 40, 
               newFileSelected ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT, 
               "New File", Gfx::ALIGN_CENTER);
    
    optionY += optionSpacing;
    bool newFolderSelected = (mContextMenuSelection == 1);
    if (newFolderSelected) {
        Gfx::DrawRectFilled(menuX + 10, optionY - 5, menuWidth - 20, 50, Gfx::COLOR_HIGHLIGHTED);
    }
    Gfx::Print(menuX + menuWidth / 2, optionY + 20, 40, 
               newFolderSelected ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT, 
               "New Folder", Gfx::ALIGN_CENTER);
    
    optionY += optionSpacing;
    bool copySelected = (mContextMenuSelection == 2);
    if (copySelected) {
        Gfx::DrawRectFilled(menuX + 10, optionY - 5, menuWidth - 20, 50, Gfx::COLOR_HIGHLIGHTED);
    }
    Gfx::Print(menuX + menuWidth / 2, optionY + 20, 40, 
               copySelected ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT, 
               "Copy", Gfx::ALIGN_CENTER);
    
    optionY += optionSpacing;
    bool moveSelected = (mContextMenuSelection == 3);
    if (moveSelected) {
        Gfx::DrawRectFilled(menuX + 10, optionY - 5, menuWidth - 20, 50, Gfx::COLOR_HIGHLIGHTED);
    }
    Gfx::Print(menuX + menuWidth / 2, optionY + 20, 40, 
               moveSelected ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT, 
               "Move", Gfx::ALIGN_CENTER);
    
    optionY += optionSpacing;
    bool pasteSelected = (mContextMenuSelection == 4);
    bool canPaste = !mClipboardPath.empty();
    if (pasteSelected) {
        Gfx::DrawRectFilled(menuX + 10, optionY - 5, menuWidth - 20, 50, Gfx::COLOR_HIGHLIGHTED);
    }
    Gfx::Print(menuX + menuWidth / 2, optionY + 20, 40, 
               canPaste ? (pasteSelected ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT) : Gfx::COLOR_ALT_TEXT, 
               "Paste", Gfx::ALIGN_CENTER);
    
    optionY += optionSpacing;
    bool renameSelected = (mContextMenuSelection == 5);
    if (renameSelected) {
        Gfx::DrawRectFilled(menuX + 10, optionY - 5, menuWidth - 20, 50, Gfx::COLOR_HIGHLIGHTED);
    }
    Gfx::Print(menuX + menuWidth / 2, optionY + 20, 40, 
               renameSelected ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT, 
               "Rename", Gfx::ALIGN_CENTER);
    
    optionY += optionSpacing;
    bool deleteSelected = (mContextMenuSelection == 6);
    if (deleteSelected) {
        Gfx::DrawRectFilled(menuX + 10, optionY - 5, menuWidth - 20, 50, Gfx::COLOR_HIGHLIGHTED);
    }
    Gfx::Print(menuX + menuWidth / 2, optionY + 20, 40, 
               deleteSelected ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT, 
               "Delete", Gfx::ALIGN_CENTER);
    
    Gfx::Print(menuX + menuWidth / 2, menuY + menuHeight - 30, 28, 
               Gfx::COLOR_ALT_TEXT, "A: Select  B: Cancel", Gfx::ALIGN_CENTER);
}

void FileManagerScreen::CreateNewFile(const std::string& filename) {
    std::string fullPath = mFileManager.GetCurrentPath();
    if (fullPath.back() != '/') {
        fullPath += "/";
    }
    fullPath += filename;
    
    std::ofstream file(fullPath);
    if (file.is_open()) {
        file.close();
        mFileManager.ScanDirectory(mFileManager.GetCurrentPath());
    }
}

void FileManagerScreen::CreateNewFolder(const std::string& foldername) {
    std::string fullPath = mFileManager.GetCurrentPath();
    if (fullPath.back() != '/') {
        fullPath += "/";
    }
    fullPath += foldername;
    
    if (mFileManager.CreateDirectory(fullPath)) {
        mFileManager.ScanDirectory(mFileManager.GetCurrentPath());
    }
}

void FileManagerScreen::DrawDeletionModal() {
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, SDL_Color{0, 0, 0, 180});
    int modalWidth = 800;
    int modalHeight = 200;
    int modalX = (Gfx::SCREEN_WIDTH - modalWidth) / 2;
    int modalY = (Gfx::SCREEN_HEIGHT - modalHeight) / 2;
    int borderWidth = 4;
    
    Gfx::DrawRectFilled(modalX, modalY, modalWidth, modalHeight, Gfx::COLOR_ALT_BACKGROUND);
    Gfx::DrawRectFilled(modalX, modalY, modalWidth, borderWidth, Gfx::COLOR_HIGHLIGHTED); // Top
    Gfx::DrawRectFilled(modalX, modalY + modalHeight - borderWidth, modalWidth, borderWidth, Gfx::COLOR_HIGHLIGHTED); // Bottom
    Gfx::DrawRectFilled(modalX, modalY, borderWidth, modalHeight, Gfx::COLOR_HIGHLIGHTED); // Left
    Gfx::DrawRectFilled(modalX + modalWidth - borderWidth, modalY, borderWidth, modalHeight, Gfx::COLOR_HIGHLIGHTED); // Right
    
    std::string message = "Deleting " + mDeletionFileName;
    Gfx::Print(modalX + modalWidth / 2, modalY + modalHeight / 2 - 30, 48, 
               Gfx::COLOR_WHITE, message, Gfx::ALIGN_CENTER);
    
    Gfx::Print(modalX + modalWidth / 2, modalY + modalHeight / 2 + 30, 40, 
               Gfx::COLOR_ALT_TEXT, "Please Wait...", Gfx::ALIGN_CENTER);
}

void FileManagerScreen::DrawLoadingModal() {
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, SDL_Color{0, 0, 0, 180});
    
    int modalWidth = 800;
    int modalHeight = 200;
    int modalX = (Gfx::SCREEN_WIDTH - modalWidth) / 2;
    int modalY = (Gfx::SCREEN_HEIGHT - modalHeight) / 2;
    int borderWidth = 4;
    
    Gfx::DrawRectFilled(modalX, modalY, modalWidth, modalHeight, Gfx::COLOR_ALT_BACKGROUND);
    Gfx::DrawRectFilled(modalX, modalY, modalWidth, borderWidth, Gfx::COLOR_HIGHLIGHTED); // Top
    Gfx::DrawRectFilled(modalX, modalY + modalHeight - borderWidth, modalWidth, borderWidth, Gfx::COLOR_HIGHLIGHTED); // Bottom
    Gfx::DrawRectFilled(modalX, modalY, borderWidth, modalHeight, Gfx::COLOR_HIGHLIGHTED); // Left
    Gfx::DrawRectFilled(modalX + modalWidth - borderWidth, modalY, borderWidth, modalHeight, Gfx::COLOR_HIGHLIGHTED); // Right
    
    Gfx::Print(modalX + modalWidth / 2, modalY + modalHeight / 2 - 30, 48, 
               Gfx::COLOR_WHITE, "Loading Directory", Gfx::ALIGN_CENTER);
    
    Gfx::Print(modalX + modalWidth / 2, modalY + modalHeight / 2 + 30, 36, 
               Gfx::COLOR_ALT_TEXT, mLoadingPath, Gfx::ALIGN_CENTER);
}

bool FileManagerScreen::ScanDirectoryWithModal(const std::string& path) {
    size_t lastSlash = path.find_last_of('/');
    mLoadingPath = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
    if (mLoadingPath.empty()) {
        mLoadingPath = path;
    }
    
    mLoadingStartTime = OSGetSystemTime();
    mShowLoadingModal = false;
    
    mFileManager.SetProgressCallback([this]() {
        uint64_t currentTime = OSGetSystemTime();
        uint64_t elapsedMs = OSTicksToMilliseconds(currentTime - mLoadingStartTime);
        
        if (elapsedMs >= 1500 && !mShowLoadingModal) {
            mShowLoadingModal = true;
            Draw();
            Gfx::Render();
        }
    });
    
    bool result = mFileManager.ScanDirectory(path);
    
    mFileManager.SetProgressCallback(nullptr);
    
    mShowLoadingModal = false;
    
    return result;
}

void FileManagerScreen::DrawLaunchConfirmModal() {
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, SDL_Color{0, 0, 0, 180});
    
    int modalWidth = 900;
    int modalHeight = 300;
    int modalX = (Gfx::SCREEN_WIDTH - modalWidth) / 2;
    int modalY = (Gfx::SCREEN_HEIGHT - modalHeight) / 2;
    int borderWidth = 4;
    
    Gfx::DrawRectFilled(modalX, modalY, modalWidth, modalHeight, Gfx::COLOR_ALT_BACKGROUND);
    Gfx::DrawRectFilled(modalX, modalY, modalWidth, borderWidth, Gfx::COLOR_HIGHLIGHTED);
    Gfx::DrawRectFilled(modalX, modalY + modalHeight - borderWidth, modalWidth, borderWidth, Gfx::COLOR_HIGHLIGHTED);
    Gfx::DrawRectFilled(modalX, modalY, borderWidth, modalHeight, Gfx::COLOR_HIGHLIGHTED);
    Gfx::DrawRectFilled(modalX + modalWidth - borderWidth, modalY, borderWidth, modalHeight, Gfx::COLOR_HIGHLIGHTED);
    
    std::string message = "Are you sure you want to open";
    Gfx::Print(modalX + modalWidth / 2, modalY + 60, 42, 
               Gfx::COLOR_WHITE, message, Gfx::ALIGN_CENTER);
    
    Gfx::Print(modalX + modalWidth / 2, modalY + 110, 44, 
               Gfx::COLOR_WHITE, mLaunchFileName + "?", Gfx::ALIGN_CENTER);
    
    int buttonY = modalY + 190;
    int buttonWidth = 200;
    int buttonHeight = 60;
    int buttonSpacing = 50;
    int yesButtonX = modalX + (modalWidth / 2) - buttonWidth - (buttonSpacing / 2);
    int noButtonX = modalX + (modalWidth / 2) + (buttonSpacing / 2);
    
    if (mLaunchModalSelection == 0) {
        Gfx::DrawRectFilled(yesButtonX, buttonY, buttonWidth, buttonHeight, Gfx::COLOR_HIGHLIGHTED);
    } else {
        Gfx::DrawRectFilled(yesButtonX, buttonY, buttonWidth, buttonHeight, Gfx::COLOR_BARS);
    }
    Gfx::Print(yesButtonX + buttonWidth / 2, buttonY + buttonHeight / 2 + 5, 40, 
               Gfx::COLOR_WHITE, "Yes!", Gfx::ALIGN_CENTER);
    
    if (mLaunchModalSelection == 1) {
        Gfx::DrawRectFilled(noButtonX, buttonY, buttonWidth, buttonHeight, Gfx::COLOR_HIGHLIGHTED);
    } else {
        Gfx::DrawRectFilled(noButtonX, buttonY, buttonWidth, buttonHeight, Gfx::COLOR_BARS);
    }
    Gfx::Print(noButtonX + buttonWidth / 2, buttonY + buttonHeight / 2 + 5, 40, 
               Gfx::COLOR_WHITE, "Never mind", Gfx::ALIGN_CENTER);
}

void FileManagerScreen::LaunchHomebrew(const std::string& path) {
    WHBLogPrintf("===========================================");
    WHBLogPrintf("LAUNCHING HOMEBREW/GAME");
    WHBLogPrintf("===========================================");
    WHBLogPrintf("Display path: %s", path.c_str());
    
    // Convert display path to real path first
    std::string realPath = PathConverter::ToRealPath(path);
    WHBLogPrintf("Real path: %s", realPath.c_str());
    
    // Check if this is a game RPX (inside a code folder with content/meta siblings)
    // Path format: /storage_usb/usr/title/00050000/101c9500/code/U-King.rpx
    bool isGameRPX = false;
    uint64_t titleId = 0;
    
    // Check if path contains "/code/" and extract title ID
    size_t codePos = realPath.find("/code/");
    if (codePos != std::string::npos) {
        // Extract the parent directory (should be the title ID folder)
        std::string beforeCode = realPath.substr(0, codePos);
        size_t lastSlash = beforeCode.find_last_of('/');
        
        if (lastSlash != std::string::npos) {
            std::string titleIdStr = beforeCode.substr(lastSlash + 1);
            WHBLogPrintf("Potential title ID: %s", titleIdStr.c_str());
            
            // Try to parse as hex title ID (8 characters for low part)
            if (titleIdStr.length() == 8) {
                try {
                    uint32_t titleIdLow = std::stoul(titleIdStr, nullptr, 16);
                    
                    // Check for high part in path (e.g., 00050000)
                    size_t titleIdPos = beforeCode.find_last_of('/', lastSlash - 1);
                    if (titleIdPos != std::string::npos) {
                        std::string highIdStr = beforeCode.substr(titleIdPos + 1, lastSlash - titleIdPos - 1);
                        WHBLogPrintf("Potential high ID: %s", highIdStr.c_str());
                        
                        if (highIdStr.length() == 8) {
                            uint32_t titleIdHigh = std::stoul(highIdStr, nullptr, 16);
                            titleId = ((uint64_t)titleIdHigh << 32) | titleIdLow;
                            isGameRPX = true;
                            WHBLogPrintf("Detected game title ID: %08X-%08X", titleIdHigh, titleIdLow);
                        }
                    }
                } catch (...) {
                    WHBLogPrintf("Failed to parse title ID");
                }
            }
        }
    }
    
    if (isGameRPX && titleId != 0) {
        WHBLogPrintf("This is a full WiiU game RPX");
        WHBLogPrintf("Title ID: %016llX", titleId);
        WHBLogPrintf("Launching game via _SYSLaunchTitleWithStdArgsInNoSplash");
        
        // Launch the game using its title ID
        // The system will automatically find the game files on USB
        _SYSLaunchTitleWithStdArgsInNoSplash(titleId, nullptr);
        
        // If we reach here, launch may have failed or is processing
        WHBLogPrintf("Launch command sent");
        WHBLogPrintf("===========================================");
        return;
    }
    
    // Not a game RPX, treat as homebrew
    WHBLogPrintf("Treating as homebrew application");
    
    bool isUSB = (realPath.find("storage_usb:/") == 0);
    std::string launchPath;
    std::string tempFilePath;
    bool needsCleanup = false;
    
    if (isUSB) {
        WHBLogPrintf("USB path detected - RPXLoader only supports SD card");
        WHBLogPrintf("Copying file to temporary SD location...");
        
        // Extract filename from path
        size_t lastSlash = realPath.find_last_of('/');
        std::string filename = (lastSlash != std::string::npos) ? realPath.substr(lastSlash + 1) : realPath;
        
        // Create temp path on SD card
        tempFilePath = "sd:/wiiu/temp_rpx_" + filename;
        
        WHBLogPrintf("Temp file: %s", tempFilePath.c_str());
        
        // Copy file from USB to SD
        std::ifstream src(realPath, std::ios::binary);
        if (!src.is_open()) {
            WHBLogPrintf("ERROR: Failed to open source file: %s", realPath.c_str());
            WHBLogPrintf("===========================================");
            return;
        }
        
        std::ofstream dst(tempFilePath, std::ios::binary);
        if (!dst.is_open()) {
            WHBLogPrintf("ERROR: Failed to create temp file: %s", tempFilePath.c_str());
            src.close();
            WHBLogPrintf("===========================================");
            return;
        }
        
        // Copy the file
        dst << src.rdbuf();
        src.close();
        dst.close();
        
        WHBLogPrintf("File copied successfully");
        
        // Use the temp file path (remove sd:/ prefix for RPXLoader)
        launchPath = "wiiu/temp_rpx_" + filename;
        needsCleanup = true;
    }
    else {
        // SD card path - normalize for RPXLoader
        if (realPath.find("sd:/") == 0) {
            launchPath = realPath.substr(4);  // Remove "sd:/"
            WHBLogPrintf("SD path detected, normalized to: %s", launchPath.c_str());
        }
        else if (realPath.find("fs:/vol/external01/") == 0) {
            launchPath = realPath.substr(19);  // Remove "fs:/vol/external01/"
            WHBLogPrintf("FS SD path detected, normalized to: %s", launchPath.c_str());
        }
        else {
            launchPath = realPath;
            WHBLogPrintf("Using path as-is: %s", launchPath.c_str());
        }
    }
    
    // Initialize RPXLoader
    RPXLoaderStatus initRes = RPXLoader_InitLibrary();
    if (initRes != RPX_LOADER_RESULT_SUCCESS) {
        WHBLogPrintf("RPXLoader_InitLibrary failed: %s", RPXLoader_GetStatusStr(initRes));
        if (needsCleanup) {
            std::remove(tempFilePath.c_str());
            WHBLogPrintf("Temp file cleaned up");
        }
        WHBLogPrintf("===========================================");
        return;
    }
    
    WHBLogPrintf("RPXLoader initialized successfully");
    WHBLogPrintf("Attempting to launch: %s", launchPath.c_str());
    
    // Launch the homebrew
    RPXLoaderStatus res = RPXLoader_LaunchHomebrew(launchPath.c_str());
    if (res != RPX_LOADER_RESULT_SUCCESS) {
        WHBLogPrintf("Failed to launch: %s", RPXLoader_GetStatusStr(res));
    } else {
        WHBLogPrintf("Launch successful!");
    }
    
    // Cleanup
    RPXLoader_DeInitLibrary();
    
    // Clean up temp file if launch failed
    if (needsCleanup && res != RPX_LOADER_RESULT_SUCCESS) {
        std::remove(tempFilePath.c_str());
        WHBLogPrintf("Temp file cleaned up");
    }
    
    WHBLogPrintf("===========================================");
}
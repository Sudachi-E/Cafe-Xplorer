#include "FileManagerScreen.hpp"
#include "TextEditorScreen.hpp"
#include "ImageViewerScreen.hpp"
#include "VideoPlayerScreen.hpp"
#include "AudioPlayerScreen.hpp"
#include "../Gfx.hpp"
#include "../utils/Keyboard.hpp"
#include <whb/log.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <fstream>
#include <coreinit/time.h>

FileManagerScreen::FileManagerScreen() : mSelectedIndex(0), mScrollOffset(0), mShowContextMenu(false), mContextMenuSelection(0), mClipboardIsDirectory(false), mClipboardIsMove(false), mShowDeletionModal(false), mShowLoadingModal(false), mLoadingStartTime(0) {
    mFileManager.ScanDirectory("fs:/vol/external01");
}

FileManagerScreen::~FileManagerScreen() = default;

void FileManagerScreen::Draw() {
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
    
    std::string leftText = "A: Select  X: Menu";
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
}

bool FileManagerScreen::Update(Input &input) {
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
    
    if (input.data.buttons_d & Input::BUTTON_X) {
        mShowContextMenu = !mShowContextMenu;
        mContextMenuSelection = 0; // Reset selection when opening menu
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
                    mVideoPlayer = std::make_unique<VideoPlayerScreen>(entry.path);
                }
                else if (IsAudioFile(entry.name)) {
                    mAudioPlayer = std::make_unique<AudioPlayerScreen>(entry.path);
                }
            }
        }
    }
    
    if (input.data.buttons_d & Input::BUTTON_B) {
        std::string currentPath = mFileManager.GetCurrentPath();
        if (currentPath != "fs:/vol/external01" && currentPath != "fs:/vol/external01/") {
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


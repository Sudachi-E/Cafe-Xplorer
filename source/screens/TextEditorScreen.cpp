#include "TextEditorScreen.hpp"
#include "../Gfx.hpp"
#include "../utils/Keyboard.hpp"
#include <fstream>
#include <sstream>

TextEditorScreen::TextEditorScreen(const std::string& filePath)
    : mFilePath(filePath), mCursorLine(0), mCursorCol(0), mScrollOffset(0),
      mModified(false), mShouldClose(false), mReadOnly(false), mMode(MODE_VIEW),
      mWaitingForKeyboard(false), mShowSaveModal(false), mSaveModalSelection(0),
      mWaitingForSaveAsPath(false) {
    
    if (!LoadFile()) {
        mLines.push_back("");
    }
}

TextEditorScreen::~TextEditorScreen() {
}

bool TextEditorScreen::LoadFile() {
    std::ifstream file(mFilePath);
    if (!file.is_open()) {
        return false;
    }
    
    mLines.clear();
    std::string line;
    while (std::getline(file, line)) {
        mLines.push_back(line);
    }
    
    file.close();
    
    if (mLines.empty()) {
        mLines.push_back("");
    }
    
    return true;
}

bool TextEditorScreen::SaveFile() {
    std::ofstream file(mFilePath);
    if (!file.is_open()) {
        return false;
    }
    
    for (size_t i = 0; i < mLines.size(); i++) {
        file << mLines[i];
        if (i < mLines.size() - 1) {
            file << "\n";
        }
    }
    
    file.close();
    mModified = false;
    return true;
}

void TextEditorScreen::Draw() {
    Gfx::Clear(Gfx::COLOR_BACKGROUND);
    
    std::string filename = mFilePath;
    size_t lastSlash = mFilePath.find_last_of('/');
    if (lastSlash != std::string::npos) {
        filename = mFilePath.substr(lastSlash + 1);
    }
    
    std::string title = filename;
    if (mModified) {
        title += " *";
    }
    
    DrawTopBar(title.c_str());
    
    int y = 100;
    int lineHeight = 40;
    int visibleLines = (Gfx::SCREEN_HEIGHT - 200) / lineHeight;
    
    if (mCursorLine < mScrollOffset) {
        mScrollOffset = mCursorLine;
    } else if (mCursorLine >= mScrollOffset + visibleLines) {
        mScrollOffset = mCursorLine - visibleLines + 1;
    }
    
    for (size_t i = mScrollOffset; i < mLines.size() && i < mScrollOffset + visibleLines; i++) {
        bool isCursorLine = (i == mCursorLine && mMode == MODE_EDIT);
        
        Gfx::DrawRectFilled(20, y - 5, 80, lineHeight, Gfx::COLOR_ALT_BACKGROUND);
        
        std::string lineNum = std::to_string(i + 1);
        Gfx::Print(60, y + 15, 28, Gfx::COLOR_ALT_TEXT, lineNum, Gfx::ALIGN_HORIZONTAL | Gfx::ALIGN_VERTICAL);
        
        if (isCursorLine) {
            Gfx::DrawRectFilled(110, y - 5, Gfx::SCREEN_WIDTH - 130, lineHeight, Gfx::COLOR_HIGHLIGHTED);
        }
        
        std::string displayLine = mLines[i];
        if (displayLine.length() > 100) {
            displayLine = displayLine.substr(0, 100) + "...";
        }
        
        Gfx::Print(120, y + 15, 28, isCursorLine ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT, 
                   displayLine, Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
        
        y += lineHeight;
    }
    
    std::string status = "Line " + std::to_string(mCursorLine + 1) + "/" + std::to_string(mLines.size());
    
    if (mWaitingForKeyboard || mWaitingForSaveAsPath) {
        DrawBottomBar("", "Keyboard active...", "");
    } else if (mMode == MODE_VIEW) {
        DrawBottomBar("Y: Edit Mode", status.c_str(), "B: Close");
    } else {
        DrawBottomBar("A: Edit  X: Delete  Y: Insert", "L: Save  R: Cancel", "B: View");
    }
    
    if (mShowSaveModal) {
        DrawSaveModal();
    }
}

void TextEditorScreen::OnKeyboardResult(bool confirmed, const std::string& text) {
    mWaitingForKeyboard = false;
    
    if (confirmed) {
        mLines[mCursorLine] = text;
        mModified = true;
    }
}

bool TextEditorScreen::Update(Input &input) {
    if (mWaitingForKeyboard || mWaitingForSaveAsPath) {
        return true;
    }
    
    if (mShowSaveModal) {
        if (input.data.buttons_d & Input::BUTTON_DOWN) {
            mSaveModalSelection = (mSaveModalSelection + 1) % 3;
        }
        
        if (input.data.buttons_d & Input::BUTTON_UP) {
            mSaveModalSelection = (mSaveModalSelection + 2) % 3;
        }
        
        if (input.data.buttons_d & Input::BUTTON_A) {
            mShowSaveModal = false;
            
            switch (mSaveModalSelection) {
                case 0:
                    if (SaveFile()) {
                        mShouldClose = true;
                    }
                    break;
                    
                case 1:
                    mWaitingForSaveAsPath = true;
                    Keyboard::RequestKeyboard(
                        mFilePath,
                        "Save as...",
                        [this](bool confirmed, const std::string& text) {
                            this->OnSaveAsKeyboardResult(confirmed, text);
                        }
                    );
                    break;
                    
                case 2:
                    mShouldClose = true;
                    break;
            }
        }
        
        if (input.data.buttons_d & Input::BUTTON_B) {
            mShowSaveModal = false;
        }
        
        return true;
    }
    
    if (mMode == MODE_VIEW) {
        if (input.data.buttons_d & Input::BUTTON_DOWN) {
            if (mCursorLine < mLines.size() - 1) {
                mCursorLine++;
            }
        }
        
        if (input.data.buttons_d & Input::BUTTON_UP) {
            if (mCursorLine > 0) {
                mCursorLine--;
            }
        }
        
        if (input.data.buttons_d & Input::BUTTON_Y) {
            // Enter edit mode
            mMode = MODE_EDIT;
        }
        
        if (input.data.buttons_d & Input::BUTTON_B) {
            if (mModified) {
                mShowSaveModal = true;
                mSaveModalSelection = 0;
            } else {
                mShouldClose = true;
            }
        }
    } else {
        if (input.data.buttons_d & Input::BUTTON_DOWN) {
            if (mCursorLine < mLines.size() - 1) {
                mCursorLine++;
            }
        }
        
        if (input.data.buttons_d & Input::BUTTON_UP) {
            if (mCursorLine > 0) {
                mCursorLine--;
            }
        }
        
        if (input.data.buttons_d & Input::BUTTON_A) {
            mWaitingForKeyboard = true;
            Keyboard::RequestKeyboard(
                mLines[mCursorLine], 
                "Edit line",
                [this](bool confirmed, const std::string& text) {
                    this->OnKeyboardResult(confirmed, text);
                }
            );
        }
        
        if (input.data.buttons_d & Input::BUTTON_X) {
            if (mLines.size() > 1) {
                mLines.erase(mLines.begin() + mCursorLine);
                if (mCursorLine >= mLines.size()) {
                    mCursorLine = mLines.size() - 1;
                }
                mModified = true;
            }
        }
        
        if (input.data.buttons_d & Input::BUTTON_Y) {
            mLines.insert(mLines.begin() + mCursorLine + 1, "");
            mCursorLine++;
            mModified = true;
        }
        
        if (input.data.buttons_d & Input::BUTTON_L) {
            if (SaveFile()) {
                mMode = MODE_VIEW;
            }
        }
        
        if (input.data.buttons_d & Input::BUTTON_R) {
            LoadFile();
            mModified = false;
            mMode = MODE_VIEW;
        }
        
        if (input.data.buttons_d & Input::BUTTON_B) {
            mMode = MODE_VIEW;
        }
    }
    
    if (input.data.buttons_d & Input::BUTTON_HOME) {
        if (mModified) {
            mShowSaveModal = true;
            mSaveModalSelection = 0;
        } else {
            mShouldClose = true;
        }
    }
    
    return !mShouldClose;
}

bool TextEditorScreen::SaveFileAs(const std::string& newPath) {
    std::ofstream file(newPath);
    if (!file.is_open()) {
        return false;
    }
    
    for (size_t i = 0; i < mLines.size(); i++) {
        file << mLines[i];
        if (i < mLines.size() - 1) {
            file << "\n";
        }
    }
    
    file.close();
    mFilePath = newPath;
    mModified = false;
    return true;
}

void TextEditorScreen::OnSaveAsKeyboardResult(bool confirmed, const std::string& text) {
    mWaitingForSaveAsPath = false;
    
    if (confirmed && !text.empty()) {
        if (SaveFileAs(text)) {
            mShouldClose = true;
        }
    }
}

void TextEditorScreen::DrawSaveModal() {
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, 
                        SDL_Color{0, 0, 0, 180});
    
    int modalWidth = 800;
    int modalHeight = 400;
    int modalX = (Gfx::SCREEN_WIDTH - modalWidth) / 2;
    int modalY = (Gfx::SCREEN_HEIGHT - modalHeight) / 2;
    int borderWidth = 4;
    
    Gfx::DrawRectFilled(modalX, modalY, modalWidth, modalHeight, Gfx::COLOR_ALT_BACKGROUND);
    Gfx::DrawRectFilled(modalX, modalY, modalWidth, borderWidth, Gfx::COLOR_HIGHLIGHTED); // Top
    Gfx::DrawRectFilled(modalX, modalY + modalHeight - borderWidth, modalWidth, borderWidth, Gfx::COLOR_HIGHLIGHTED); // Bottom
    Gfx::DrawRectFilled(modalX, modalY, borderWidth, modalHeight, Gfx::COLOR_HIGHLIGHTED); // Left
    Gfx::DrawRectFilled(modalX + modalWidth - borderWidth, modalY, borderWidth, modalHeight, Gfx::COLOR_HIGHLIGHTED); // Right

    Gfx::Print(modalX + modalWidth / 2, modalY + 50, 36, Gfx::COLOR_TEXT,
               "Save changes?", Gfx::ALIGN_CENTER);
    
    const char* options[] = {
        "Save and close",
        "Save as and close",
        "Close without saving"
    };
    
    int optionY = modalY + 140;
    int optionHeight = 60;
    
    for (int i = 0; i < 3; i++) {
        bool isSelected = (i == mSaveModalSelection);
        
        if (isSelected) {
            Gfx::DrawRectFilled(modalX + 50, optionY - 5, modalWidth - 100, 
                              optionHeight, Gfx::COLOR_HIGHLIGHTED);
        }
        
        Gfx::Print(modalX + modalWidth / 2, optionY + optionHeight / 2, 32,
                   isSelected ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT,
                   options[i], Gfx::ALIGN_CENTER);
        
        optionY += optionHeight + 10;
    }
    
    Gfx::Print(modalX + modalWidth / 2, modalY + modalHeight - 40, 28, 
               Gfx::COLOR_ALT_TEXT, "A: Select  B: Cancel",
               Gfx::ALIGN_CENTER);
}

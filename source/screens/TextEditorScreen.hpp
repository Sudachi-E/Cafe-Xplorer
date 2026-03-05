#pragma once

#include "../Screen.hpp"
#include <string>
#include <vector>

class TextEditorScreen : public Screen {
public:
    explicit TextEditorScreen(const std::string& filePath);
    ~TextEditorScreen() override;
    
    void Draw() override;
    bool Update(Input &input) override;
    
    bool ShouldClose() const { return mShouldClose; }

private:
    enum Mode {
        MODE_VIEW,
        MODE_EDIT
    };
    
    bool LoadFile();
    bool SaveFile();
    bool SaveFileAs(const std::string& newPath);
    void OnKeyboardResult(bool confirmed, const std::string& text);
    void OnSaveAsKeyboardResult(bool confirmed, const std::string& text);
    void DrawSaveModal();
    
    std::string mFilePath;
    std::vector<std::string> mLines;
    size_t mCursorLine;
    size_t mCursorCol;
    size_t mScrollOffset;
    bool mModified;
    bool mShouldClose;
    bool mReadOnly;
    Mode mMode;
    bool mWaitingForKeyboard;
    bool mShowSaveModal;
    int mSaveModalSelection;
    bool mWaitingForSaveAsPath;
};

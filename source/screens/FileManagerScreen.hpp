#pragma once

#include "../Screen.hpp"
#include "../filemanager/FileManager.h"
#include <memory>

class TextEditorScreen;
class ImageViewerScreen;
class VideoPlayerScreen;
class AudioPlayerScreen;

class FileManagerScreen : public Screen {
public:
    FileManagerScreen();
    ~FileManagerScreen() override;
    void Draw() override;
    bool Update(Input &input) override;

private:
    FileManager mFileManager;
    size_t mSelectedIndex;
    size_t mScrollOffset;
    std::unique_ptr<TextEditorScreen> mTextEditor;
    std::unique_ptr<ImageViewerScreen> mImageViewer;
    std::unique_ptr<VideoPlayerScreen> mVideoPlayer;
    std::unique_ptr<AudioPlayerScreen> mAudioPlayer;
    bool mShowContextMenu;
    int mContextMenuSelection;
    std::string mClipboardPath;
    bool mClipboardIsDirectory;
    bool mClipboardIsMove;
    std::string mLastVisitedDir;
    bool mShowDeletionModal;
    std::string mDeletionFileName;
    bool mShowLoadingModal;
    std::string mLoadingPath;
    uint64_t mLoadingStartTime;
    
    static std::string FormatSize(size_t bytes);
    static bool IsTextFile(const std::string& filename);
    static bool IsImageFile(const std::string& filename);
    static bool IsVideoFile(const std::string& filename);
    static bool IsAudioFile(const std::string& filename);
    void DrawContextMenu();
    void DrawDeletionModal();
    void DrawLoadingModal();
    void CreateNewFile(const std::string& filename);
    void CreateNewFolder(const std::string& foldername);
    bool ScanDirectoryWithModal(const std::string& path);
};

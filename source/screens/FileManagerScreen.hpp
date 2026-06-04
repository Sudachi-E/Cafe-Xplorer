#pragma once

#include "../Screen.hpp"
#include "../filemanager/FileManager.h"
#include <memory>
#include <set>

class TextEditorScreen;
class ImageViewerScreen;
class GifViewerScreen;
class PdfViewerScreen;
class VideoPlayerScreen;
class AudioPlayerScreen;
class SettingsScreen;

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
    std::unique_ptr<GifViewerScreen> mGifViewer;
    std::unique_ptr<PdfViewerScreen> mPdfViewer;
    std::unique_ptr<VideoPlayerScreen> mVideoPlayer;
    std::unique_ptr<AudioPlayerScreen> mAudioPlayer;
    std::unique_ptr<SettingsScreen> mSettingsScreen;
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
    bool mShowLaunchConfirmModal;
    std::string mLaunchFileName;
    std::string mLaunchFilePath;
    int mLaunchModalSelection;
    uint64_t mLastUpdateTick;
    float mHoldTimer;
    float mRepeatAccum;
    
    bool mShowCopyProgressModal;
    uint64_t mCopyProgressBytes;
    uint64_t mCopyProgressTotal;
    std::string mCopyProgressName;
    
    bool mShowDeleteConfirmModal;
    int mDeleteConfirmSelection;
    std::vector<std::string> mPendingDeletePaths;
    std::vector<bool> mPendingDeleteIsDirectories;
    std::vector<std::string> mPendingDeleteFileNames;
    
    bool mSelectionMode;
    std::set<size_t> mSelectedIndices;
    std::vector<std::string> mMultiClipboardPaths;
    std::vector<bool> mMultiClipboardIsDirectory;
    bool mMultiClipboardIsMove;

    static std::string FormatSize(size_t bytes);
    static bool IsTextFile(const std::string& filename);
    static bool IsImageFile(const std::string& filename);
    static bool IsVideoFile(const std::string& filename);
    static bool IsAudioFile(const std::string& filename);
    static bool IsRPXFile(const std::string& filename);
    static bool IsWUHBFile(const std::string& filename);
    void DrawContextMenu();
    void DrawDeletionModal();
    void DrawDeleteConfirmModal();
    void DrawLoadingModal();
    void DrawLaunchConfirmModal();
    void DrawCopyProgressModal();
    void CreateNewFile(const std::string& filename);
    void CreateNewFolder(const std::string& foldername);
    bool ScanDirectoryWithModal(const std::string& path);
    void LaunchHomebrew(const std::string& path);
};

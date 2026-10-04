#pragma once

#include "../Screen.hpp"
#include "../Gfx.hpp"
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
    enum class PendingAction {
        None,
        Paste,
        Rename,
        CreateFile,
        CreateFolder
    };

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
    bool mCopyProgressIsMove;
    
    bool mShowDeleteConfirmModal;
    int mDeleteConfirmSelection;
    std::vector<std::string> mPendingDeletePaths;
    std::vector<bool> mPendingDeleteIsDirectories;
    std::vector<std::string> mPendingDeleteFileNames;

    bool mShowOverwriteModal;
    int mOverwriteSelection;
    std::vector<std::string> mPendingOverwritePaths;
    size_t mPendingOverwriteIndex;
    PendingAction mPendingOverwriteAction;
    std::string mPendingTargetPath;
    std::string mPendingTargetName;
    std::string mPendingDuplicatePath;

    std::vector<std::string> mPendingPastePaths;
    std::vector<bool> mPendingPasteIsDirectories;
    bool mPendingPasteIsMove;
    
    bool mSelectionMode;
    std::set<size_t> mSelectedIndices;
    std::vector<std::string> mMultiClipboardPaths;
    std::vector<bool> mMultiClipboardIsDirectory;
    bool mMultiClipboardIsMove;

    bool mShowFtpModal;
    std::string mFtpIp;

    static bool IsTextFile(const std::string& filename);
    static bool IsImageFile(const std::string& filename);
    static bool IsVideoFile(const std::string& filename);
    static bool IsAudioFile(const std::string& filename);
    static bool IsRPXFile(const std::string& filename);
    static bool IsWUHBFile(const std::string& filename);
    static void DrawEntryIcon(const FileEntry& entry, int x, int y, int size, SDL_Color color);
    void DrawContextMenu();
    void DrawDeletionModal();
    void DrawDeleteConfirmModal();
    void DrawOverwriteModal();
    void DrawLoadingModal();
    void DrawLaunchConfirmModal();
    void DrawCopyProgressModal();
    void DrawFtpModal();
    void CreateNewFile(const std::string& filename);
    void CreateNewFolder(const std::string& foldername);
    void AskToOverwrite(PendingAction action, const std::string& existingPath, const std::string& newName = "");
    void RunPendingAction(PendingAction action, const std::string& targetPath, const std::string& targetName);
    int OverwriteOptionCount() const;
    void StartPaste();
    void QueuePaste(const std::vector<std::string>& paths, const std::vector<bool>& isDirectories, bool isMove);
    bool PerformPaste();
    void ClearPendingPaste();
    void ClearPendingOverwrite();
    bool ScanDirectoryWithModal(const std::string& path);
    void LaunchHomebrew(const std::string& path);
};

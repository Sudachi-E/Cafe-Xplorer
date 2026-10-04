#pragma once

#include <string>
#include <vector>
#include <functional>
#include <ctime>

static constexpr const char* DATE_TEXT_SAMPLE = "2026-09-28 19:35 PM";

struct FileEntry {
    std::string name;
    std::string path;
    bool isDirectory;
    size_t size;
    bool isHidden;
    std::string displayName;
    std::string sizeText;
    time_t modifiedTime = 0;
    std::string dateText;
};

class FileManager {
public:
    enum class OpResult {
        Success,
        Failed,
        Cancelled
    };

    FileManager();
    bool ScanDirectory(const std::string& path);
    const std::vector<FileEntry>& GetEntries() const { return mEntries; }
    std::string GetCurrentPath() const { return mCurrentPath; }
    bool NavigateUp();
    bool DeleteEntry(const std::string& path, bool isDirectory);
    bool CreateFile(const std::string& path);
    bool CreateDirectory(const std::string& path);
    OpResult PasteEntry(const std::string& sourcePath, const std::string& destDir, bool isDirectory);
    OpResult MoveEntry(const std::string& sourcePath, const std::string& destDir, bool isDirectory);
    bool RenameEntry(const std::string& oldPath, const std::string& newName);

    void CollectPasteConflicts(const std::string& sourcePath, const std::string& destDir, bool isDirectory,
                               std::vector<std::string>& conflicts);

    void ApproveOverwrite(const std::string& path);
    void ClearApprovedOverwrites();

    static bool PathExists(const std::string& path, bool* isDirectory = nullptr);

    static std::string JoinPath(const std::string& dir, const std::string& name);

    static std::string GenerateUniqueName(const std::string& dir, const std::string& filename);

    void SetProgressCallback(std::function<void()> callback) { mProgressCallback = callback; }

    using CopyProgressCallback = std::function<void(uint64_t, uint64_t)>;
    void SetCopyProgressCallback(CopyProgressCallback callback) { mCopyProgressCallback = callback; }

    static uint64_t CalculateTotalSize(const std::string& realPath);

private:
    std::string ResolveDestinationPath(const std::string& sourcePath, const std::string& destDir);

    OpResult ResolveDestinationConflict(const std::string& destPath, const std::string& destName);
    std::vector<FileEntry> mEntries;
    std::string mCurrentPath;
    
    std::function<void()> mProgressCallback;
    CopyProgressCallback mCopyProgressCallback;
    std::vector<std::string> mApprovedOverwrites;

    OpResult PasteEntryInternal(const std::string& sourcePath, const std::string& destDir,
                                bool isDirectory, uint64_t& bytesCopied, uint64_t totalBytes);
    void CollectPasteConflictsInternal(const std::string& sourcePath, const std::string& destDir,
                                       bool isDirectory, std::vector<std::string>& conflicts);
};

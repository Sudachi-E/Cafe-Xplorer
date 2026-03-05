#pragma once

#include <string>
#include <vector>
#include <functional>

struct FileEntry {
    std::string name;
    std::string path;
    bool isDirectory;
    size_t size;
};

class FileManager {
public:
    FileManager();
    bool ScanDirectory(const std::string& path);
    const std::vector<FileEntry>& GetEntries() const { return mEntries; }
    std::string GetCurrentPath() const { return mCurrentPath; }
    bool NavigateUp();
    bool DeleteEntry(const std::string& path, bool isDirectory);
    bool CreateDirectory(const std::string& path);
    bool PasteEntry(const std::string& sourcePath, const std::string& destDir, bool isDirectory);
    bool MoveEntry(const std::string& sourcePath, const std::string& destDir, bool isDirectory);
    bool RenameEntry(const std::string& oldPath, const std::string& newName);
    
    bool HasMoreEntries() const { return mHasMoreEntries; }
    bool LoadMoreEntries();
    size_t GetTotalEntryCount() const { return mTotalEntryCount; }
    
    void SetProgressCallback(std::function<void()> callback) { mProgressCallback = callback; }

private:
    std::vector<FileEntry> mEntries;
    std::string mCurrentPath;
    
    std::vector<std::string> mPendingEntries;  // Names of entries not yet loaded
    bool mHasMoreEntries;
    size_t mTotalEntryCount;
    static const size_t ENTRIES_PER_LOAD = 50;  // Load 50 entries at a time
    
    std::function<void()> mProgressCallback;
};

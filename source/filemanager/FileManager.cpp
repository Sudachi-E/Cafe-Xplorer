#include "FileManager.h"
#include "../utils/logger.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <whb/log.h>

FileManager::FileManager() : mCurrentPath("fs:/vol/external01"), mHasMoreEntries(false), mTotalEntryCount(0) {}

bool FileManager::ScanDirectory(const std::string& path) {
    mEntries.clear();
    mPendingEntries.clear();
    mHasMoreEntries = false;
    mTotalEntryCount = 0;
    
    WHBLogPrintf("Attempting to open directory: %s", path.c_str());
    
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        WHBLogPrintf("Failed to open directory: %s", path.c_str());
        return false;
    }

    WHBLogPrintf("Successfully opened directory: %s", path.c_str());
    
    mCurrentPath = path;
    
    struct dirent* entry;
    std::vector<std::string> allEntryNames;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip . and .. entries
        if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") {
            continue;
        }
        allEntryNames.push_back(entry->d_name);
        
        if (mProgressCallback && allEntryNames.size() % 100 == 0) {
            mProgressCallback();
        }
    }
    closedir(dir);
    
    mTotalEntryCount = allEntryNames.size();
    WHBLogPrintf("Found %zu total entries in directory", mTotalEntryCount);
    
    std::sort(allEntryNames.begin(), allEntryNames.end());
    
    size_t entriesToLoad = std::min(ENTRIES_PER_LOAD, allEntryNames.size());
    
    for (size_t i = 0; i < entriesToLoad; i++) {
        FileEntry fileEntry;
        fileEntry.name = allEntryNames[i];
        
        if (path.back() == '/') {
            fileEntry.path = path + allEntryNames[i];
        } else {
            fileEntry.path = path + "/" + allEntryNames[i];
        }
        
        struct stat st;
        if (stat(fileEntry.path.c_str(), &st) == 0) {
            fileEntry.isDirectory = S_ISDIR(st.st_mode);
            fileEntry.size = st.st_size;
        } else {
            fileEntry.isDirectory = false;
            fileEntry.size = 0;
        }
        
        mEntries.push_back(fileEntry);
        
        if (mProgressCallback && i % 10 == 0) {
            mProgressCallback();
        }
    }
    
    for (size_t i = entriesToLoad; i < allEntryNames.size(); i++) {
        mPendingEntries.push_back(allEntryNames[i]);
    }
    
    mHasMoreEntries = !mPendingEntries.empty();
    
    std::sort(mEntries.begin(), mEntries.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory;
        }
        return a.name < b.name;
    });
    
    WHBLogPrintf("Loaded %zu entries initially, %zu pending", mEntries.size(), mPendingEntries.size());
    
    return true;
}

bool FileManager::LoadMoreEntries() {
    if (!mHasMoreEntries || mPendingEntries.empty()) {
        return false;
    }
    
    size_t entriesToLoad = std::min(ENTRIES_PER_LOAD, mPendingEntries.size());
    WHBLogPrintf("Loading %zu more entries...", entriesToLoad);
    
    for (size_t i = 0; i < entriesToLoad; i++) {
        FileEntry fileEntry;
        fileEntry.name = mPendingEntries[i];
        
        if (mCurrentPath.back() == '/') {
            fileEntry.path = mCurrentPath + mPendingEntries[i];
        } else {
            fileEntry.path = mCurrentPath + "/" + mPendingEntries[i];
        }
        
        struct stat st;
        if (stat(fileEntry.path.c_str(), &st) == 0) {
            fileEntry.isDirectory = S_ISDIR(st.st_mode);
            fileEntry.size = st.st_size;
        } else {
            fileEntry.isDirectory = false;
            fileEntry.size = 0;
        }
        
        mEntries.push_back(fileEntry);
    }
    
    mPendingEntries.erase(mPendingEntries.begin(), mPendingEntries.begin() + entriesToLoad);
    
    mHasMoreEntries = !mPendingEntries.empty();
    
    std::sort(mEntries.begin(), mEntries.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory;
        }
        return a.name < b.name;
    });
    
    WHBLogPrintf("Now have %zu entries loaded, %zu pending", mEntries.size(), mPendingEntries.size());
    
    return true;
}

bool FileManager::NavigateUp() {
    if (mCurrentPath == "fs:/vol/external01" || mCurrentPath == "fs:/vol/external01/") {
        return false;
    }
    
    size_t lastSlash = mCurrentPath.find_last_of('/');
    if (lastSlash == std::string::npos) {
        mCurrentPath = "fs:/vol/external01";
    } else {
        std::string newPath = mCurrentPath.substr(0, lastSlash);
        // Don't go above the SD card root
        if (newPath.length() < 18) { // "fs:/vol/external01"
            mCurrentPath = "fs:/vol/external01";
        } else {
            mCurrentPath = newPath;
        }
    }
    
    return ScanDirectory(mCurrentPath);
}

bool FileManager::DeleteEntry(const std::string& path, bool isDirectory) {
    WHBLogPrintf("Attempting to delete: %s (isDir: %d)", path.c_str(), isDirectory);

    if (isDirectory) {
        DIR* dir = opendir(path.c_str());
        if (!dir) {
            WHBLogPrintf("Failed to open directory for deletion: %s", path.c_str());
            return false;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") {
                continue;
            }

            std::string entryPath = path;
            if (path.back() != '/') {
                entryPath += "/";
            }
            entryPath += entry->d_name;

            struct stat st;
            bool entryIsDir = false;
            if (stat(entryPath.c_str(), &st) == 0) {
                entryIsDir = S_ISDIR(st.st_mode);
            }

            if (!DeleteEntry(entryPath, entryIsDir)) {
                closedir(dir);
                return false;
            }
        }
        closedir(dir);

        if (rmdir(path.c_str()) != 0) {
            WHBLogPrintf("Failed to delete directory: %s", path.c_str());
            return false;
        }
    } else {
        if (remove(path.c_str()) != 0) {
            WHBLogPrintf("Failed to delete file: %s", path.c_str());
            return false;
        }
    }

    WHBLogPrintf("Successfully deleted: %s", path.c_str());
    return true;
}

bool FileManager::CreateDirectory(const std::string& path) {
    WHBLogPrintf("Attempting to create directory: %s", path.c_str());
    
    if (mkdir(path.c_str(), 0777) != 0) {
        WHBLogPrintf("Failed to create directory: %s", path.c_str());
        return false;
    }
    
    WHBLogPrintf("Successfully created directory: %s", path.c_str());
    return true;
}

bool FileManager::PasteEntry(const std::string& sourcePath, const std::string& destDir, bool isDirectory) {
    WHBLogPrintf("Attempting to paste: %s to %s (isDir: %d)", sourcePath.c_str(), destDir.c_str(), isDirectory);
    
    size_t lastSlash = sourcePath.find_last_of('/');
    std::string filename = (lastSlash != std::string::npos) ? sourcePath.substr(lastSlash + 1) : sourcePath;
    
    std::string destPath = destDir;
    if (destPath.back() != '/') {
        destPath += "/";
    }
    destPath += filename;
    
    if (sourcePath == destPath) {
        WHBLogPrintf("Source and destination are the same, skipping");
        return false;
    }
    
    if (isDirectory) {
        if (mkdir(destPath.c_str(), 0777) != 0) {
            WHBLogPrintf("Failed to create destination directory: %s", destPath.c_str());
            return false;
        }
        
        DIR* dir = opendir(sourcePath.c_str());
        if (!dir) {
            WHBLogPrintf("Failed to open source directory: %s", sourcePath.c_str());
            return false;
        }
        
        struct dirent* entry;
        bool success = true;
        while ((entry = readdir(dir)) != nullptr) {
            if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") {
                continue;
            }
            
            std::string entrySourcePath = sourcePath;
            if (sourcePath.back() != '/') {
                entrySourcePath += "/";
            }
            entrySourcePath += entry->d_name;
            
            struct stat st;
            bool entryIsDir = false;
            if (stat(entrySourcePath.c_str(), &st) == 0) {
                entryIsDir = S_ISDIR(st.st_mode);
            }
            
            if (!PasteEntry(entrySourcePath, destPath, entryIsDir)) {
                success = false;
                break;
            }
        }
        closedir(dir);
        
        return success;
    } else {
        std::ifstream src(sourcePath.c_str(), std::ios::binary);
        if (!src.is_open()) {
            WHBLogPrintf("Failed to open source file: %s", sourcePath.c_str());
            return false;
        }
        
        std::ofstream dst(destPath.c_str(), std::ios::binary);
        if (!dst.is_open()) {
            WHBLogPrintf("Failed to create destination file: %s", destPath.c_str());
            return false;
        }
        
        dst << src.rdbuf();
        
        if (!dst.good()) {
            WHBLogPrintf("Error writing to destination file: %s", destPath.c_str());
            return false;
        }
        
        WHBLogPrintf("Successfully copied file: %s to %s", sourcePath.c_str(), destPath.c_str());
        return true;
    }
}

bool FileManager::MoveEntry(const std::string& sourcePath, const std::string& destDir, bool isDirectory) {
    WHBLogPrintf("Attempting to move: %s to %s (isDir: %d)", sourcePath.c_str(), destDir.c_str(), isDirectory);
    
    size_t lastSlash = sourcePath.find_last_of('/');
    std::string filename = (lastSlash != std::string::npos) ? sourcePath.substr(lastSlash + 1) : sourcePath;
    
    std::string destPath = destDir;
    if (destPath.back() != '/') {
        destPath += "/";
    }
    destPath += filename;
    
    if (sourcePath == destPath) {
        WHBLogPrintf("Source and destination are the same, skipping");
        return false;
    }
    
    if (rename(sourcePath.c_str(), destPath.c_str()) == 0) {
        WHBLogPrintf("Successfully moved using rename: %s to %s", sourcePath.c_str(), destPath.c_str());
        return true;
    }
    
    WHBLogPrintf("Rename failed, falling back to copy+delete");
    if (PasteEntry(sourcePath, destDir, isDirectory)) {
        if (DeleteEntry(sourcePath, isDirectory)) {
            WHBLogPrintf("Successfully moved using copy+delete: %s to %s", sourcePath.c_str(), destPath.c_str());
            return true;
        } else {
            WHBLogPrintf("Failed to delete source after copy: %s", sourcePath.c_str());
            return false;
        }
    }
    
    WHBLogPrintf("Failed to move: %s to %s", sourcePath.c_str(), destPath.c_str());
    return false;
}

bool FileManager::RenameEntry(const std::string& oldPath, const std::string& newName) {
    WHBLogPrintf("Attempting to rename: %s to %s", oldPath.c_str(), newName.c_str());

    size_t lastSlash = oldPath.find_last_of('/');
    if (lastSlash == std::string::npos) {
        WHBLogPrintf("Invalid path format: %s", oldPath.c_str());
        return false;
    }

    std::string directory = oldPath.substr(0, lastSlash);

    std::string newPath = directory;
    if (newPath.back() != '/') {
        newPath += "/";
    }
    newPath += newName;

    if (oldPath == newPath) {
        WHBLogPrintf("Source and destination are the same, skipping");
        return false;
    }

    if (rename(oldPath.c_str(), newPath.c_str()) != 0) {
        WHBLogPrintf("Failed to rename: %s to %s", oldPath.c_str(), newPath.c_str());
        return false;
    }

    WHBLogPrintf("Successfully renamed: %s to %s", oldPath.c_str(), newPath.c_str());
    return true;
}

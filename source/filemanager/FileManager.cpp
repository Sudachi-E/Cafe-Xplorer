#include "FileManager.h"
#include "PathConverter.hpp"
#include "../utils/logger.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <whb/log.h>

FileManager::FileManager() : mCurrentPath("/"), mHasMoreEntries(false), mTotalEntryCount(0) {
    PathConverter::Initialize();
}

bool FileManager::ScanDirectory(const std::string& path) {
    mEntries.clear();
    mPendingEntries.clear();
    mHasMoreEntries = false;
    mTotalEntryCount = 0;
    
    WHBLogPrintf("Attempting to open directory: %s", path.c_str());
    
    if (path == "/" || path.empty()) {
        if (PathConverter::IsVirtualDirectory("/")) {
            WHBLogPrintf("Opening virtual root directory");
            mCurrentPath = "/";
            
            auto subdirs = PathConverter::GetVirtualSubdirs("/");
            for (const auto& subdir : subdirs) {
                FileEntry fileEntry;
                fileEntry.name = subdir;
                fileEntry.path = "/" + subdir;
                fileEntry.isDirectory = true;
                fileEntry.size = 0;
                mEntries.push_back(fileEntry);
            }
            
            mTotalEntryCount = mEntries.size();
            WHBLogPrintf("Virtual root directory has %zu entries", mTotalEntryCount);
            return true;
        }
    }
    
    std::string realPath = PathConverter::ToRealPath(path);
    WHBLogPrintf("Converted path: %s -> %s", path.c_str(), realPath.c_str());
    
    DIR* dir = opendir(realPath.c_str());
    
    if (!dir) {
        WHBLogPrintf("Failed to open real directory: %s", realPath.c_str());
        
        if (PathConverter::IsVirtualDirectory(path)) {
            WHBLogPrintf("Opening virtual directory: %s", path.c_str());
            mCurrentPath = path;
            
            auto subdirs = PathConverter::GetVirtualSubdirs(path);
            for (const auto& subdir : subdirs) {
                FileEntry fileEntry;
                fileEntry.name = subdir;
                if (path == "/") {
                    fileEntry.path = "/" + subdir;
                } else {
                    fileEntry.path = path + "/" + subdir;
                }
                fileEntry.isDirectory = true;
                fileEntry.size = 0;
                mEntries.push_back(fileEntry);
            }
            
            mTotalEntryCount = mEntries.size();
            WHBLogPrintf("Virtual directory has %zu entries", mTotalEntryCount);
            return true;
        }
        
        WHBLogPrintf("Not a virtual directory either, giving up");
        return false;
    }

    WHBLogPrintf("Successfully opened directory: %s", realPath.c_str());
    
    mCurrentPath = path;
    WHBLogPrintf("Set current path to: %s", mCurrentPath.c_str());
    
    struct dirent* entry;
    std::vector<std::string> allEntryNames;
    while ((entry = readdir(dir)) != nullptr) {
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
        
        std::string realEntryPath = PathConverter::ToRealPath(fileEntry.path);
        struct stat st;
        if (stat(realEntryPath.c_str(), &st) == 0) {
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
        
        std::string realEntryPath = PathConverter::ToRealPath(fileEntry.path);
        struct stat st;
        if (stat(realEntryPath.c_str(), &st) == 0) {
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
    if (mCurrentPath == "/" || mCurrentPath.empty()) {
        return false;
    }
    
    size_t lastSlash = mCurrentPath.find_last_of('/');
    if (lastSlash == std::string::npos || lastSlash == 0) {
        mCurrentPath = "/";
    } else {
        mCurrentPath = mCurrentPath.substr(0, lastSlash);
        if (mCurrentPath.empty()) {
            mCurrentPath = "/";
        }
    }
    
    return ScanDirectory(mCurrentPath);
}

bool FileManager::DeleteEntry(const std::string& path, bool isDirectory) {
    WHBLogPrintf("Attempting to delete: %s (isDir: %d)", path.c_str(), isDirectory);

    std::string realPath = PathConverter::ToRealPath(path);
    WHBLogPrintf("Real path for deletion: %s", realPath.c_str());

    if (isDirectory) {
        DIR* dir = opendir(realPath.c_str());
        if (!dir) {
            WHBLogPrintf("Failed to open directory for deletion: %s", realPath.c_str());
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

            std::string realEntryPath = PathConverter::ToRealPath(entryPath);
            struct stat st;
            bool entryIsDir = false;
            if (stat(realEntryPath.c_str(), &st) == 0) {
                entryIsDir = S_ISDIR(st.st_mode);
            }

            if (!DeleteEntry(entryPath, entryIsDir)) {
                closedir(dir);
                return false;
            }
        }
        closedir(dir);

        if (rmdir(realPath.c_str()) != 0) {
            WHBLogPrintf("Failed to delete directory: %s", realPath.c_str());
            return false;
        }
    } else {
        if (remove(realPath.c_str()) != 0) {
            WHBLogPrintf("Failed to delete file: %s", realPath.c_str());
            return false;
        }
    }

    WHBLogPrintf("Successfully deleted: %s", path.c_str());
    return true;
}

bool FileManager::CreateDirectory(const std::string& path) {
    WHBLogPrintf("Attempting to create directory: %s", path.c_str());
    
    std::string realPath = PathConverter::ToRealPath(path);
    if (mkdir(realPath.c_str(), 0777) != 0) {
        WHBLogPrintf("Failed to create directory: %s", realPath.c_str());
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
    
    std::string realSourcePath = PathConverter::ToRealPath(sourcePath);
    std::string realDestPath = PathConverter::ToRealPath(destPath);
    
    if (isDirectory) {
        if (mkdir(realDestPath.c_str(), 0777) != 0) {
            WHBLogPrintf("Failed to create destination directory: %s", realDestPath.c_str());
            return false;
        }
        
        DIR* dir = opendir(realSourcePath.c_str());
        if (!dir) {
            WHBLogPrintf("Failed to open source directory: %s", realSourcePath.c_str());
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
            
            std::string realEntrySourcePath = PathConverter::ToRealPath(entrySourcePath);
            struct stat st;
            bool entryIsDir = false;
            if (stat(realEntrySourcePath.c_str(), &st) == 0) {
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
        std::ifstream src(realSourcePath.c_str(), std::ios::binary);
        if (!src.is_open()) {
            WHBLogPrintf("Failed to open source file: %s", realSourcePath.c_str());
            return false;
        }
        
        std::ofstream dst(realDestPath.c_str(), std::ios::binary);
        if (!dst.is_open()) {
            WHBLogPrintf("Failed to create destination file: %s", realDestPath.c_str());
            return false;
        }
        
        dst << src.rdbuf();
        
        if (!dst.good()) {
            WHBLogPrintf("Error writing to destination file: %s", realDestPath.c_str());
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
    
    std::string realSourcePath = PathConverter::ToRealPath(sourcePath);
    std::string realDestPath = PathConverter::ToRealPath(destPath);
    
    if (rename(realSourcePath.c_str(), realDestPath.c_str()) == 0) {
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

    std::string realOldPath = PathConverter::ToRealPath(oldPath);
    std::string realNewPath = PathConverter::ToRealPath(newPath);

    if (rename(realOldPath.c_str(), realNewPath.c_str()) != 0) {
        WHBLogPrintf("Failed to rename: %s to %s", oldPath.c_str(), newPath.c_str());
        return false;
    }

    WHBLogPrintf("Successfully renamed: %s to %s", oldPath.c_str(), newPath.c_str());
    return true;
}

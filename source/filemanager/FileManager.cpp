#include "FileManager.h"
#include "PathConverter.hpp"
#include "../utils/DateTimeUtils.hpp"
#include "../utils/logger.h"
#include "../utils/Settings.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <whb/log.h>

static std::string FormatSizeText(size_t bytes) {
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

static std::string FormatDateText(time_t modifiedTime) {
    if (modifiedTime <= 0) {
        return "--";
    }

    struct tm brokenDown;
    if (!gmtime_r(&modifiedTime, &brokenDown)) {
        return "--";
    }
    return FormatCalendarTime(brokenDown);
}

static void PopulateFileEntryDisplay(FileEntry& fileEntry) {
    fileEntry.isHidden = !fileEntry.name.empty() && fileEntry.name[0] == '.';
    fileEntry.displayName = fileEntry.name;
    fileEntry.sizeText = fileEntry.isDirectory ? std::string() : FormatSizeText(fileEntry.size);
    fileEntry.dateText = FormatDateText(fileEntry.modifiedTime);
}

std::string FileManager::JoinPath(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == "/") {
        return "/" + name;
    }
    if (dir.back() == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

static std::string GetFileName(const std::string& path) {
    size_t lastSlash = path.find_last_of('/');
    return (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);
}

static std::string GetParentPath(const std::string& path) {
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash == std::string::npos) {
        return "";
    }
    if (lastSlash == 0) {
        return "/";
    }
    return path.substr(0, lastSlash);
}

static std::string GetDirectoryPath(const std::string& dir) {
    if (dir.empty()) {
        return "/";
    }
    std::string directory = dir;
    while (directory.size() > 1 && directory.back() == '/') {
        directory.pop_back();
    }
    return directory;
}

static bool IsSameOrInsideDirectory(const std::string& path, const std::string& directory) {
    std::string normalizedPath = GetDirectoryPath(path);
    std::string normalizedDirectory = GetDirectoryPath(directory);
    if (normalizedPath == normalizedDirectory) {
        return true;
    }
    return normalizedPath.size() > normalizedDirectory.size() &&
           normalizedPath.compare(0, normalizedDirectory.size(), normalizedDirectory) == 0 &&
           normalizedPath[normalizedDirectory.size()] == '/';
}

FileManager::FileManager() : mCurrentPath("/") {
    PathConverter::Initialize();
}

bool FileManager::ScanDirectory(const std::string& path) {
    mEntries.clear();
    
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
                PopulateFileEntryDisplay(fileEntry);
                mEntries.push_back(fileEntry);
            }
            
            WHBLogPrintf("Virtual root directory has %zu entries", mEntries.size());
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
                PopulateFileEntryDisplay(fileEntry);
                mEntries.push_back(fileEntry);
            }
            
            WHBLogPrintf("Virtual directory has %zu entries", mEntries.size());
            return true;
        }
        
        WHBLogPrintf("Not a virtual directory either, giving up");
        return false;
    }

    WHBLogPrintf("Successfully opened directory: %s", realPath.c_str());
    
    mCurrentPath = path;
    WHBLogPrintf("Set current path to: %s", mCurrentPath.c_str());
    
    struct dirent* entry;
    bool showHidden = Settings::GetShowHiddenFiles();
    while ((entry = readdir(dir)) != nullptr) {
        if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") {
            continue;
        }
        if (!showHidden && entry->d_name[0] == '.') {
            continue;
        }
        
        FileEntry fileEntry;
        fileEntry.name = entry->d_name;
        
        if (path.back() == '/') {
            fileEntry.path = path + entry->d_name;
        } else {
            fileEntry.path = path + "/" + entry->d_name;
        }
        
        std::string realEntryPath = PathConverter::ToRealPath(fileEntry.path);
        struct stat st;
        if (stat(realEntryPath.c_str(), &st) == 0) {
            fileEntry.isDirectory = S_ISDIR(st.st_mode);
            fileEntry.size = st.st_size;
            fileEntry.modifiedTime = st.st_mtime;
        } else {
            fileEntry.isDirectory = false;
            fileEntry.size = 0;
        }

        PopulateFileEntryDisplay(fileEntry);

        mEntries.push_back(fileEntry);
    }
    closedir(dir);
    
    std::sort(mEntries.begin(), mEntries.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory;
        }
        return a.name < b.name;
    });

    for (auto& entry : mEntries) {
        PopulateFileEntryDisplay(entry);
    }
    
    WHBLogPrintf("Loaded %zu entries from directory", mEntries.size());
    
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

bool FileManager::CreateFile(const std::string& path) {
    WHBLogPrintf("Attempting to create file: %s", path.c_str());

    if (ResolveDestinationConflict(path, GetFileName(path)) != OpResult::Success) {
        WHBLogPrintf("Failed to create file: %s", path.c_str());
        return false;
    }

    std::ofstream file(PathConverter::ToRealPath(path));
    if (!file.is_open()) {
        WHBLogPrintf("Failed to create file: %s", path.c_str());
        return false;
    }
    file.close();

    WHBLogPrintf("Successfully created file: %s", path.c_str());
    return true;
}

bool FileManager::CreateDirectory(const std::string& path) {
    WHBLogPrintf("Attempting to create directory: %s", path.c_str());

    if (ResolveDestinationConflict(path, GetFileName(path)) != OpResult::Success) {
        WHBLogPrintf("Failed to create directory: %s", path.c_str());
        return false;
    }

    std::string realPath = PathConverter::ToRealPath(path);
    if (mkdir(realPath.c_str(), 0777) != 0) {
        WHBLogPrintf("Failed to create directory: %s", realPath.c_str());
        return false;
    }
    
    WHBLogPrintf("Successfully created directory: %s", path.c_str());
    return true;
}

bool FileManager::PathExists(const std::string& path, bool* isDirectory) {
    if (isDirectory) {
        *isDirectory = false;
    }
    if (path.empty()) {
        return false;
    }

    struct stat st;
    if (stat(PathConverter::ToRealPath(path).c_str(), &st) != 0) {
        return false;
    }

    if (isDirectory) {
        *isDirectory = S_ISDIR(st.st_mode);
    }
    return true;
}

std::string FileManager::GenerateUniqueName(const std::string& dir, const std::string& filename) {
    std::string base = filename;
    std::string extension;

    size_t dot = filename.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        base = filename.substr(0, dot);
        extension = filename.substr(dot);
    }

    std::string candidate = filename;
    for (int counter = 1; counter < 10000; counter++) {
        if (!PathExists(JoinPath(dir, candidate))) {
            break;
        }
        candidate = base + "(" + std::to_string(counter) + ")" + extension;
    }
    return candidate;
}

std::string FileManager::ResolveDestinationPath(const std::string& sourcePath, const std::string& destDir) {
    std::string filename = GetFileName(sourcePath);

    if (GetParentPath(sourcePath) == GetDirectoryPath(destDir)) {
        filename = GenerateUniqueName(destDir, filename);
    }

    return JoinPath(destDir, filename);
}

void FileManager::ApproveOverwrite(const std::string& path) {
    mApprovedOverwrites.push_back(path);
}

void FileManager::ClearApprovedOverwrites() {
    mApprovedOverwrites.clear();
}

FileManager::OpResult FileManager::ResolveDestinationConflict(const std::string& destPath,
                                                              const std::string& destName) {
    bool destIsDirectory = false;
    if (!PathExists(destPath, &destIsDirectory)) {
        return OpResult::Success;
    }

    if (std::find(mApprovedOverwrites.begin(), mApprovedOverwrites.end(), destPath) == mApprovedOverwrites.end()) {
        WHBLogPrintf("Destination already exists and was not approved: %s", destName.c_str());
        return OpResult::Cancelled;
    }

    WHBLogPrintf("Replacing existing entry: %s", destName.c_str());
    if (!DeleteEntry(destPath, destIsDirectory)) {
        WHBLogPrintf("Failed to remove existing entry: %s", destPath.c_str());
        return OpResult::Failed;
    }

    return OpResult::Success;
}

FileManager::OpResult FileManager::PasteEntry(const std::string& sourcePath, const std::string& destDir, bool isDirectory) {
    WHBLogPrintf("Attempting to paste: %s to %s (isDir: %d)", sourcePath.c_str(), destDir.c_str(), isDirectory);

    // Calculate total size upfront so progress is reported
    std::string realSourcePath = PathConverter::ToRealPath(sourcePath);
    uint64_t totalBytes = CalculateTotalSize(realSourcePath);
    WHBLogPrintf("Total bytes to copy: %llu", (unsigned long long)totalBytes);

    uint64_t bytesCopied = 0;
    return PasteEntryInternal(sourcePath, destDir, isDirectory, bytesCopied, totalBytes);
}

uint64_t FileManager::CalculateTotalSize(const std::string& realPath) {
    struct stat st;
    if (stat(realPath.c_str(), &st) != 0) {
        return 0;
    }

    if (S_ISDIR(st.st_mode)) {
        uint64_t total = 0;
        DIR* dir = opendir(realPath.c_str());
        if (!dir) return 0;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") continue;
            std::string childPath = realPath;
            if (childPath.back() != '/') childPath += "/";
            childPath += entry->d_name;
            total += CalculateTotalSize(childPath);
        }
        closedir(dir);
        return total;
    } else {
        return static_cast<uint64_t>(st.st_size);
    }
}

FileManager::OpResult FileManager::PasteEntryInternal(const std::string& sourcePath, const std::string& destDir,
                                                      bool isDirectory, uint64_t& bytesCopied, uint64_t totalBytes) {
    if (isDirectory && IsSameOrInsideDirectory(destDir, sourcePath)) {
        WHBLogPrintf("Refusing to copy a folder into itself: %s", sourcePath.c_str());
        return OpResult::Failed;
    }

    std::string filename = GetFileName(sourcePath);
    std::string destPath = ResolveDestinationPath(sourcePath, destDir);

    if (sourcePath == destPath) {
        WHBLogPrintf("Source and destination are the same, skipping");
        return OpResult::Failed;
    }

    std::string realSourcePath = PathConverter::ToRealPath(sourcePath);
    std::string realDestPath   = PathConverter::ToRealPath(destPath);

    if (isDirectory) {
        bool destIsDirectory = false;
        bool destExists = PathExists(destPath, &destIsDirectory);
        if (destExists && !destIsDirectory) {
            OpResult conflict = ResolveDestinationConflict(destPath, filename);
            if (conflict != OpResult::Success) {
                return conflict;
            }
            destExists = false;
        }

        if (!destExists) {
            if (mkdir(realDestPath.c_str(), 0777) != 0) {
                WHBLogPrintf("Failed to create destination directory: %s", realDestPath.c_str());
                return OpResult::Failed;
            }
        }

        DIR* dir = opendir(realSourcePath.c_str());
        if (!dir) {
            WHBLogPrintf("Failed to open source directory: %s", realSourcePath.c_str());
            return OpResult::Failed;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") continue;

            std::string entrySourcePath = JoinPath(sourcePath, entry->d_name);

            struct stat st;
            bool entryIsDir = false;
            if (stat(PathConverter::ToRealPath(entrySourcePath).c_str(), &st) == 0) {
                entryIsDir = S_ISDIR(st.st_mode);
            }

            OpResult result = PasteEntryInternal(entrySourcePath, destPath, entryIsDir, bytesCopied, totalBytes);
            if (result != OpResult::Success) {
                closedir(dir);
                return result;
            }
        }
        closedir(dir);
        return OpResult::Success;
    } else {
        OpResult conflict = ResolveDestinationConflict(destPath, filename);
        if (conflict != OpResult::Success) {
            return conflict;
        }

        // Copy file in chunks so progress is reported correctly
        static const size_t CHUNK_SIZE = 256 * 1024;
        std::vector<char> buffer(CHUNK_SIZE);
        std::ifstream src(realSourcePath.c_str(), std::ios::binary);
        if (!src.is_open()) {
            WHBLogPrintf("Failed to open source file: %s", realSourcePath.c_str());
            return OpResult::Failed;
        }

        std::ofstream dst(realDestPath.c_str(), std::ios::binary);
        if (!dst.is_open()) {
            WHBLogPrintf("Failed to create destination file: %s", realDestPath.c_str());
            return OpResult::Failed;
        }

        while (src) {
            src.read(buffer.data(), CHUNK_SIZE);
            std::streamsize bytesRead = src.gcount();
            if (bytesRead <= 0) break;

            dst.write(buffer.data(), bytesRead);
            if (!dst.good()) {
                WHBLogPrintf("Error writing to destination file: %s", realDestPath.c_str());
                return OpResult::Failed;
            }

            bytesCopied += static_cast<uint64_t>(bytesRead);

            if (mCopyProgressCallback) {
                mCopyProgressCallback(bytesCopied, totalBytes);
            }
        }

        if (!dst.good() && !dst.eof()) {
            WHBLogPrintf("Error writing to destination file: %s", realDestPath.c_str());
            return OpResult::Failed;
        }

        WHBLogPrintf("Successfully copied file: %s to %s", sourcePath.c_str(), destPath.c_str());
        return OpResult::Success;
    }
}

void FileManager::CollectPasteConflicts(const std::string& sourcePath, const std::string& destDir,
                                        bool isDirectory, std::vector<std::string>& conflicts) {
    CollectPasteConflictsInternal(sourcePath, destDir, isDirectory, conflicts);
}

void FileManager::CollectPasteConflictsInternal(const std::string& sourcePath, const std::string& destDir,
                                                bool isDirectory, std::vector<std::string>& conflicts) {
    if (isDirectory && IsSameOrInsideDirectory(destDir, sourcePath)) {
        return;
    }

    std::string destPath = ResolveDestinationPath(sourcePath, destDir);

    bool destIsDirectory = false;
    if (!PathExists(destPath, &destIsDirectory)) {
        return;
    }

    if (isDirectory && destIsDirectory) {
        DIR* dir = opendir(PathConverter::ToRealPath(sourcePath).c_str());
        if (!dir) {
            return;
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") continue;

            std::string entrySourcePath = JoinPath(sourcePath, entry->d_name);

            struct stat st;
            bool entryIsDir = false;
            if (stat(PathConverter::ToRealPath(entrySourcePath).c_str(), &st) == 0) {
                entryIsDir = S_ISDIR(st.st_mode);
            }

            CollectPasteConflictsInternal(entrySourcePath, destPath, entryIsDir, conflicts);
        }
        closedir(dir);
        return;
    }

    conflicts.push_back(destPath);
}

FileManager::OpResult FileManager::MoveEntry(const std::string& sourcePath, const std::string& destDir, bool isDirectory) {
    WHBLogPrintf("Attempting to move: %s to %s (isDir: %d)", sourcePath.c_str(), destDir.c_str(), isDirectory);

    if (isDirectory && IsSameOrInsideDirectory(destDir, sourcePath)) {
        WHBLogPrintf("Refusing to move a folder into itself: %s", sourcePath.c_str());
        return OpResult::Failed;
    }

    std::string filename = GetFileName(sourcePath);
    std::string destPath = JoinPath(destDir, filename);

    if (sourcePath == destPath) {
        WHBLogPrintf("Source and destination are the same, skipping");
        return OpResult::Failed;
    }

    if (GetParentPath(sourcePath) == GetDirectoryPath(destDir)) {
        WHBLogPrintf("Source and destination folder are the same, skipping move");
        return OpResult::Failed;
    }

    bool destIsDirectory = false;
    if (PathExists(destPath, &destIsDirectory)) {
        if (!isDirectory || !destIsDirectory) {
            OpResult conflict = ResolveDestinationConflict(destPath, filename);
            if (conflict != OpResult::Success) {
                return conflict;
            }
        }
    }

    std::string realSourcePath = PathConverter::ToRealPath(sourcePath);
    std::string realDestPath = PathConverter::ToRealPath(destPath);
    if (rename(realSourcePath.c_str(), realDestPath.c_str()) == 0) {
        WHBLogPrintf("Successfully moved using rename: %s to %s", sourcePath.c_str(), destPath.c_str());
        return OpResult::Success;
    }

    WHBLogPrintf("Rename failed, falling back to copy+delete");
    OpResult copied = PasteEntry(sourcePath, destDir, isDirectory);
    if (copied != OpResult::Success) {
        return copied;
    }

    if (DeleteEntry(sourcePath, isDirectory)) {
        WHBLogPrintf("Successfully moved using copy+delete: %s to %s", sourcePath.c_str(), destPath.c_str());
        return OpResult::Success;
    }

    WHBLogPrintf("Failed to delete source after copy: %s", sourcePath.c_str());
    return OpResult::Failed;
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

    if (ResolveDestinationConflict(newPath, newName) != OpResult::Success) {
        WHBLogPrintf("Failed to rename: %s to %s", oldPath.c_str(), newPath.c_str());
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

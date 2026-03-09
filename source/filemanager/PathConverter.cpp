#include "PathConverter.hpp"
#include <algorithm>

std::map<std::string, std::vector<std::string>> PathConverter::sVirtualDirs;
bool PathConverter::sInitialized = false;

void PathConverter::Initialize() {
    if (sInitialized) return;
    
    sVirtualDirs["/"] = {};
    
    sVirtualDirs["/fs"] = {"vol"};
    
    sVirtualDirs["/fs/vol"] = {"external01", "content", "save"};
    
    sVirtualDirs["/fs/vol/content"] = {};
    sVirtualDirs["/fs/vol/save"] = {};
    
    sInitialized = true;
}

void PathConverter::AddRootDirectory(const std::string& dirName) {
    if (!sInitialized) Initialize();
    sVirtualDirs["/"].push_back(dirName);
    
    std::string virtualPath = "/" + dirName;
    if (sVirtualDirs.find(virtualPath) == sVirtualDirs.end()) {
        sVirtualDirs[virtualPath] = {};
    }
}

void PathConverter::ClearRootDirectory() {
    if (!sInitialized) Initialize();
    sVirtualDirs["/"].clear();
}

std::string PathConverter::ToRealPath(const std::string& displayPath) {
    if (displayPath == "/" || displayPath.empty()) {
        return "/";
    }
    
    std::string path = displayPath;
    if (path.back() == '/' && path.length() > 1) {
        path = path.substr(0, path.length() - 1);
    }
    
    size_t secondSlash = path.find('/', 1);
    
    if (secondSlash != std::string::npos) {
        std::string prefix = path.substr(1, secondSlash - 1);
        std::string suffix = path.substr(secondSlash);
        return prefix + ":" + suffix;
    } else {
        return path.substr(1) + ":/";
    }
}

std::string PathConverter::ToDisplayPath(const std::string& realPath) {
    size_t colonPos = realPath.find(':');
    if (colonPos != std::string::npos) {
        std::string prefix = realPath.substr(0, colonPos);
        std::string suffix = realPath.substr(colonPos + 1);
        
        if (suffix == "/") {
            return "/" + prefix;
        }
        return "/" + prefix + suffix;
    }
    
    return realPath;
}

bool PathConverter::IsVirtualDirectory(const std::string& displayPath) {
    if (!sInitialized) Initialize();
    return sVirtualDirs.find(displayPath) != sVirtualDirs.end();
}

std::vector<std::string> PathConverter::GetVirtualSubdirs(const std::string& displayPath) {
    if (!sInitialized) Initialize();
    
    auto it = sVirtualDirs.find(displayPath);
    if (it != sVirtualDirs.end()) {
        return it->second;
    }
    return {};
}

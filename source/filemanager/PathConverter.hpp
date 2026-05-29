#pragma once
#include <string>
#include <vector>
#include <map>

class PathConverter {
public:
    static std::string ToRealPath(const std::string& displayPath);
    
    static std::string ToDisplayPath(const std::string& realPath);
    
    static bool IsVirtualDirectory(const std::string& displayPath);
    
    static std::vector<std::string> GetVirtualSubdirs(const std::string& displayPath);
    
    static void Initialize();
    
    static void AddRootDirectory(const std::string& dirName);
    
    static void AddRedirect(const std::string& displayName, const std::string& devoptabPath);
    
    static void ClearRootDirectory();
    
private:
    static std::map<std::string, std::vector<std::string>> sVirtualDirs;
    static std::map<std::string, std::string> sRedirects;
    static bool sInitialized;
};

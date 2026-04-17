#include "FileScanner.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

FileScanner::FileScanner()
{
}

#include <algorithm>
#include <set>

std::vector<std::string> FileScanner::scanDirectory(const std::string& path, bool recursive)
{
    std::vector<std::string> files;
    try {
        if (recursive) {
            for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied);
                 it != fs::recursive_directory_iterator(); ++it) {
                
                if (isIgnored(it->path())) {
                    if (it->is_directory()) {
                        it.disable_recursion_pending();
                    }
                    continue;
                }

                if (it->is_regular_file()) {
                    files.push_back(it->path().string());
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied)) {
                if (isIgnored(entry.path())) continue;
                
                if (entry.is_regular_file()) {
                    files.push_back(entry.path().string());
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error scanning directory: " << e.what() << std::endl;
    }
    return files;
}

bool FileScanner::isIgnored(const std::filesystem::path& path)
{
    std::string filename = path.filename().string();
    
    // Ignored directories (exact match)
    static const std::set<std::string> ignoredDirs = {
        ".git", ".vs", ".vscode", ".idea", ".smartfile", 
        "build", "bin", "obj", "debug", "release", 
        "__pycache__", "node_modules", "target",
        "Steam", "steamapps", "Program Files", "Program Files (x86)", 
        "Windows", "System32", "AppData"
    };

    // Ignored automatically if hidden on Unix (starts with a dot)
    if (!filename.empty() && filename[0] == '.') return true;

    // Ignored extensions (lowercase)
    static const std::set<std::string> ignoredExts = {
        ".obj", ".o", ".lib", ".a", ".dll", ".exe", ".so", ".dylib",
        ".pdb", ".ilk", ".exp", ".idb", ".pch", 
        ".cmake", ".sln", ".vcxproj", ".vcxproj.filters", ".vcxproj.user",
        ".log", ".tlog", ".ninja", ".qm", ".ts",
        ".lnk", ".url", ".sys", ".iso", ".msi" 
    };

    if (fs::is_directory(path)) {
        if (ignoredDirs.count(filename)) return true;
    } else {
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ignoredExts.count(ext)) return true;
    }

    return false;
}

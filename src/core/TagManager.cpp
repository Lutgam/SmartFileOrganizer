#include "TagManager.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <QDebug>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

TagManager::TagManager() {
}

void TagManager::loadTags(const std::string& directory) {
    currentDirectory = directory;
    metadataFile = getMetadataPath();
    m_tagToFilePaths.clear();
    m_fileToTags.clear();
    
    // We start fresh (user request)
}

void TagManager::saveTags() {
    if (currentDirectory.empty()) return;

    std::string smartfileDir = currentDirectory + "/.smartfile";
    if (!fs::exists(smartfileDir)) {
        fs::create_directory(smartfileDir);
    }
#ifdef _WIN32
    SetFileAttributesA(smartfileDir.c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif

    try {
        nlohmann::json root = nlohmann::json::object();
        for (const auto& [file, tags] : m_fileToTags) {
            nlohmann::json arr = nlohmann::json::array();
            for (const QString& tag : tags) {
                arr.push_back(tag.toStdString());
            }
            root[file.toStdString()] = arr;
        }

        std::ofstream f(metadataFile);
        f << root.dump(4);
    } catch (const std::exception& e) {
        std::cerr << "Error saving metadata: " << e.what() << std::endl;
    }
}

void TagManager::addTag(const QString& filename, const QString& tag, bool save) {
    m_fileToTags[filename].insert(tag);
    m_tagToFilePaths[tag].insert(filename);
    if (save) {
        saveTags();
    }
}

void TagManager::removeTag(const QString& filename, const QString& tag) {
    if (m_fileToTags.count(filename)) {
        m_fileToTags[filename].erase(tag);
        if (m_fileToTags[filename].empty()) m_fileToTags.erase(filename);
    }
    if (m_tagToFilePaths.count(tag)) {
        m_tagToFilePaths[tag].erase(filename);
        if (m_tagToFilePaths[tag].empty()) m_tagToFilePaths.erase(tag);
    }
    saveTags();
}

void TagManager::renameTag(const QString& oldTag, const QString& newTag) {
    if (m_tagToFilePaths.count(oldTag)) {
        std::set<QString> files = m_tagToFilePaths[oldTag];
        m_tagToFilePaths.erase(oldTag);
        for (const QString& file : files) {
            m_fileToTags[file].erase(oldTag);
            m_fileToTags[file].insert(newTag);
            m_tagToFilePaths[newTag].insert(file);
        }
        saveTags();
    }
}

void TagManager::deleteTag(const QString& tag) {
    if (m_tagToFilePaths.count(tag)) {
        std::set<QString> files = m_tagToFilePaths[tag];
        m_tagToFilePaths.erase(tag);
        for (const QString& file : files) {
            m_fileToTags[file].erase(tag);
            if (m_fileToTags[file].empty()) m_fileToTags.erase(file);
        }
        saveTags();
    }
}

std::vector<QString> TagManager::getTags(const QString& filename) const {
    std::vector<QString> res;
    auto it = m_fileToTags.find(filename);
    if (it != m_fileToTags.end()) {
        res.assign(it->second.begin(), it->second.end());
    }
    return res;
}

void TagManager::setTags(const QString& filename, const std::vector<QString>& tags) {
    // Remove old mappings
    if (m_fileToTags.count(filename)) {
        for (const QString& oldTag : m_fileToTags[filename]) {
            m_tagToFilePaths[oldTag].erase(filename);
            if (m_tagToFilePaths[oldTag].empty()) m_tagToFilePaths.erase(oldTag);
        }
    }
    m_fileToTags[filename].clear();

    // Add new ones
    for (const QString& tag : tags) {
        m_fileToTags[filename].insert(tag);
        m_tagToFilePaths[tag].insert(filename);
    }
    saveTags();
}

std::vector<QString> TagManager::getAllTags() const {
    std::vector<QString> result;
    for (const auto& pair : m_tagToFilePaths) {
        result.push_back(pair.first);
    }
    return result;
}

std::vector<QString> TagManager::getFilesByTag(const QString& tag) const {
    std::vector<QString> result;
    auto it = m_tagToFilePaths.find(tag);
    if (it != m_tagToFilePaths.end()) {
        result.assign(it->second.begin(), it->second.end());
    }
    return result;
}

std::string TagManager::getMetadataPath() const {
    return currentDirectory + "/.smartfile/metadata.json";
}

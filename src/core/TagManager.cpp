#include "TagManager.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <QDebug>
#include <QMutexLocker>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

TagManager::TagManager() {
}

void TagManager::loadTags(const std::string& directory) {
    QMutexLocker locker(&m_mutex);
    currentDirectory = directory;
    metadataFile = getMetadataPath();
    m_tagToFilePaths.clear();
    m_fileToTags.clear();
    m_rejectedTags.clear();
    
    // We start fresh (user request)
    loadRejectedTags();
}

void TagManager::saveTags() {
    QMutexLocker locker(&m_mutex);
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

    // Persist rejected tags as well
    saveRejectedTags();
}

void TagManager::addTag(const QString& filename, const QString& tag, bool save) {
    QMutexLocker locker(&m_mutex);
    m_fileToTags[filename].insert(tag);
    m_tagToFilePaths[tag].insert(filename);
    if (save) {
        saveTags();
    }
}

void TagManager::removeTag(const QString& filename, const QString& tag) {
    QMutexLocker locker(&m_mutex);
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
    QMutexLocker locker(&m_mutex);
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
    QMutexLocker locker(&m_mutex);
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

std::vector<std::pair<QString, QString>> TagManager::taggedFilesWithPrimaryTag() const {
    QMutexLocker locker(&m_mutex);
    std::vector<std::pair<QString, QString>> out;
    out.reserve(m_fileToTags.size());
    for (const auto& entry : m_fileToTags) {
        if (entry.second.empty()) {
            continue;
        }
        const QString primary = *entry.second.begin();
        out.emplace_back(entry.first, primary);
    }
    return out;
}

void TagManager::relocateFilePath(const QString& oldPath, const QString& newPath, bool saveMetadata) {
    QMutexLocker locker(&m_mutex);
    if (oldPath == newPath) {
        return;
    }
    auto it = m_fileToTags.find(oldPath);
    if (it == m_fileToTags.end()) {
        return;
    }
    std::set<QString> tags = std::move(it->second);
    m_fileToTags.erase(it);
    m_fileToTags[newPath] = std::move(tags);

    for (const QString& t : m_fileToTags[newPath]) {
        m_tagToFilePaths[t].erase(oldPath);
        m_tagToFilePaths[t].insert(newPath);
    }

    if (saveMetadata) {
        saveTags();
    }
}

std::vector<QString> TagManager::getTags(const QString& filename) const {
    QMutexLocker locker(&m_mutex);
    std::vector<QString> res;
    auto it = m_fileToTags.find(filename);
    if (it != m_fileToTags.end()) {
        res.assign(it->second.begin(), it->second.end());
    }
    return res;
}

void TagManager::setTags(const QString& filename, const std::vector<QString>& tags) {
    QMutexLocker locker(&m_mutex);
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
    QMutexLocker locker(&m_mutex);
    std::vector<QString> result;
    for (const auto& pair : m_tagToFilePaths) {
        result.push_back(pair.first);
    }
    return result;
}

std::vector<QString> TagManager::getFilesByTag(const QString& tag) const {
    QMutexLocker locker(&m_mutex);
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

std::string TagManager::getRejectedTagsPath() const {
    return currentDirectory + "/.smartfile/rejected_tags.json";
}

void TagManager::loadRejectedTags() {
    if (currentDirectory.empty()) return;
    const std::string path = getRejectedTagsPath();
    if (!fs::exists(path)) return;
    try {
        std::ifstream f(path);
        nlohmann::json root;
        f >> root;
        if (!root.is_array()) return;
        for (const auto& v : root) {
            if (!v.is_string()) continue;
            const QString t = QString::fromStdString(v.get<std::string>()).trimmed();
            if (!t.isEmpty()) m_rejectedTags.insert(t);
        }
    } catch (const std::exception& e) {
        qDebug() << "Error loading rejected tags:" << e.what();
    }
}

void TagManager::saveRejectedTags() const {
    if (currentDirectory.empty()) return;
    const std::string smartfileDir = currentDirectory + "/.smartfile";
    if (!fs::exists(smartfileDir)) {
        fs::create_directory(smartfileDir);
    }
#ifdef _WIN32
    SetFileAttributesA(smartfileDir.c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif

    try {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : m_rejectedTags) {
            arr.push_back(t.toStdString());
        }
        std::ofstream f(getRejectedTagsPath());
        f << arr.dump(2);
    } catch (const std::exception& e) {
        qDebug() << "Error saving rejected tags:" << e.what();
    }
}

void TagManager::addRejectedTag(const QString& tag) {
    QMutexLocker locker(&m_mutex);
    const QString t = tag.trimmed();
    if (t.isEmpty()) return;
    m_rejectedTags.insert(t);
    saveRejectedTags();
}

QStringList TagManager::getRejectedTags() const {
    QMutexLocker locker(&m_mutex);
    QStringList out;
    for (const auto& t : m_rejectedTags) out << t;
    out.sort(Qt::CaseInsensitive);
    return out;
}

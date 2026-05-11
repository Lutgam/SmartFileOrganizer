#include "TagManager.h"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMutexLocker>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

// Leading `[` … `AI` … `]` with optional internal/outer whitespace (handles "[ AI ]", "[AI]資料庫", etc.).
// Anchored, case-insensitive. Used only with QString::remove.
const QRegularExpression &aiPrefixStripRegex()
{
    static const QRegularExpression re(QStringLiteral("^\\[\\s*AI\\s*\\]\\s*"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

} // namespace

QString TagManager::stripAiPrefix(const QString &tag)
{
    QString s = tag.trimmed();
    s.remove(aiPrefixStripRegex());
    return s.trimmed();
}

bool TagManager::hasAiPrefix(const QString &tag)
{
    const auto m = aiPrefixStripRegex().match(tag.trimmed());
    return m.hasMatch() && m.capturedStart() == 0;
}

void TagManager::repairMalformedTagKeys()
{
    QMutexLocker locker(&m_mutex);
    std::vector<QString> keys;
    keys.reserve(m_tagToFilePaths.size());
    for (const auto &p : m_tagToFilePaths) keys.push_back(p.first);

    bool changed = false;
    for (const QString &oldK : keys) {
        if (!m_tagToFilePaths.count(oldK)) continue;
        const QString newK = normalizeTag(oldK);
        if (newK.isEmpty() || newK == oldK) continue;

        auto it = m_tagToFilePaths.find(oldK);
        if (it == m_tagToFilePaths.end()) continue;
        const std::set<QString> files = it->second;
        for (const QString &fp : files) {
            if (m_fileToTags.count(fp)) {
                m_fileToTags[fp].erase(oldK);
                m_fileToTags[fp].insert(newK);
            } else {
                m_fileToTags[fp].insert(newK);
            }
            m_tagToFilePaths[newK].insert(fp);
        }
        m_tagToFilePaths.erase(oldK);
        if (m_rejectedTags.count(oldK)) {
            m_rejectedTags.erase(oldK);
        }
        changed = true;
    }
    if (changed) saveTags();
}

TagManager::TagManager() {
}

QString TagManager::normalizeTag(const QString &tag) const {
    QString t = tag.trimmed();
    if (t.isEmpty()) return QString();

    // Repair legacy corruption: "[ 資料庫" (only "[" + spaces left after bad stripping of "AI]").
    if (!hasAiPrefix(t)) {
        static const QRegularExpression corruptedOpenBracket(QStringLiteral("^\\[\\s+"));
        if (corruptedOpenBracket.match(t).hasMatch()) {
            QString u = t;
            u.remove(corruptedOpenBracket);
            t = u.trimmed();
        }
    }
    if (t.isEmpty()) return QString();

    auto resolveSynonym = [](const QString &lowerOrZh) -> QString {
        // Map common AI synonyms / English tags to system preset tags (ZH).
        static const QHash<QString, QString> map = []() {
            QHash<QString, QString> m;
            // database
            m.insert(QStringLiteral("database"), QStringLiteral("資料庫"));
            m.insert(QStringLiteral("db"), QStringLiteral("資料庫"));
            m.insert(QStringLiteral("sql"), QStringLiteral("資料庫"));
            m.insert(QStringLiteral("資料庫"), QStringLiteral("資料庫"));

            // document
            m.insert(QStringLiteral("document"), QStringLiteral("文件"));
            m.insert(QStringLiteral("documents"), QStringLiteral("文件"));
            m.insert(QStringLiteral("pdf"), QStringLiteral("文件"));
            m.insert(QStringLiteral("word"), QStringLiteral("文件"));
            m.insert(QStringLiteral("text"), QStringLiteral("文件"));
            m.insert(QStringLiteral("文件"), QStringLiteral("文件"));

            // image
            m.insert(QStringLiteral("image"), QStringLiteral("圖片"));
            m.insert(QStringLiteral("images"), QStringLiteral("圖片"));
            m.insert(QStringLiteral("picture"), QStringLiteral("圖片"));
            m.insert(QStringLiteral("photo"), QStringLiteral("圖片"));
            m.insert(QStringLiteral("圖片"), QStringLiteral("圖片"));

            // code
            m.insert(QStringLiteral("code"), QStringLiteral("程式碼"));
            m.insert(QStringLiteral("script"), QStringLiteral("程式碼"));
            m.insert(QStringLiteral("source"), QStringLiteral("程式碼"));
            m.insert(QStringLiteral("source code"), QStringLiteral("程式碼"));
            m.insert(QStringLiteral("程式碼"), QStringLiteral("程式碼"));

            // video
            m.insert(QStringLiteral("video"), QStringLiteral("影片"));
            m.insert(QStringLiteral("movie"), QStringLiteral("影片"));
            m.insert(QStringLiteral("影片"), QStringLiteral("影片"));

            return m;
        }();

        const auto it = map.constFind(lowerOrZh);
        if (it == map.constEnd()) return QString();
        return it.value();
    };

    // Order: trim -> fold case for matching -> strip `[AI]` via regex (never index-based mid/remove)
    // -> synonym map on clean payload -> if AI-tagged and no synonym, re-attach `[AI] ` prefix.
    const QString folded = t.toLower();
    QString core = folded;
    core.remove(aiPrefixStripRegex());
    core = core.trimmed();

    const bool hadAiPrefix = hasAiPrefix(folded);

    if (hadAiPrefix) {
        const QString mapped = resolveSynonym(core);
        if (!mapped.isEmpty())
            return mapped; // system preset: no [AI] prefix
        if (core.isEmpty())
            return QString();
        return QStringLiteral("[AI] ") + core;
    }

    const QString mapped = resolveSynonym(core);
    if (!mapped.isEmpty()) return mapped;
    return folded;
}

void TagManager::loadTags(const std::string& directory) {
    QMutexLocker locker(&m_mutex);
    currentDirectory = directory;
    metadataFile = getMetadataPath();
    m_tagToFilePaths.clear();
    m_fileToTags.clear();
    m_rejectedTags.clear();
    m_pathToContentHash.clear();
    m_hashAnalysisCache.clear();
    m_tagParents.clear();

    loadRejectedTags();

    if (currentDirectory.empty() || metadataFile.empty()) return;
    if (!fs::exists(metadataFile)) return;

    try {
        std::ifstream f(metadataFile);
        nlohmann::json root;
        f >> root;
        if (!root.is_object()) return;

        const bool isV2 = root.contains("schema_version") && root["schema_version"].is_number_integer()
                          && root["schema_version"].get<int>() == 2 && root.contains("files")
                          && root["files"].is_object();

        auto ingestTagsForPath = [this](const QString &path, const nlohmann::json &tagsArr) {
            if (!tagsArr.is_array()) return;
            for (const auto &tv : tagsArr) {
                if (!tv.is_string()) continue;
                const QString nt = normalizeTag(QString::fromStdString(tv.get<std::string>()));
                if (nt.isEmpty()) continue;
                m_fileToTags[path].insert(nt);
                m_tagToFilePaths[nt].insert(path);
            }
        };

        if (isV2) {
            const auto &filesObj = root["files"];
            for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
                const QString path = QString::fromStdString(it.key());
                const auto &val = it.value();
                if (val.is_array()) {
                    ingestTagsForPath(path, val);
                    continue;
                }
                if (!val.is_object()) continue;
                const auto &obj = val;
                if (obj.contains("content_sha256") && obj["content_sha256"].is_string()) {
                    const QString hx = QString::fromStdString(obj["content_sha256"].get<std::string>());
                    if (!hx.isEmpty()) m_pathToContentHash[path] = hx;
                }
                if (obj.contains("tags") && obj["tags"].is_array()) ingestTagsForPath(path, obj["tags"]);
            }
            if (root.contains("hash_analysis_cache") && root["hash_analysis_cache"].is_object()) {
                for (auto it = root["hash_analysis_cache"].begin(); it != root["hash_analysis_cache"].end();
                     ++it) {
                    const QString hx = QString::fromStdString(it.key());
                    if (hx.isEmpty()) continue;
                    m_hashAnalysisCache[hx] = it.value();
                }
            }
            if (root.contains("tag_parents") && root["tag_parents"].is_object()) {
                for (auto it = root["tag_parents"].begin(); it != root["tag_parents"].end(); ++it) {
                    if (!it.value().is_string()) continue;
                    const QString child = normalizeTag(QString::fromStdString(it.key()));
                    const QString parent = normalizeTag(QString::fromStdString(it.value().get<std::string>()));
                    if (child.isEmpty() || parent.isEmpty()) continue;
                    m_tagParents[child] = parent;
                }
            }
        } else {
            // Legacy: top-level keys are absolute paths → JSON array of tag strings.
            for (auto it = root.begin(); it != root.end(); ++it) {
                if (!it.value().is_array()) continue;
                const QString path = QString::fromStdString(it.key());
                ingestTagsForPath(path, it.value());
            }
        }
    } catch (const std::exception &e) {
        qDebug() << "Error loading metadata.json:" << e.what();
    }
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
        root["schema_version"] = 2;
        nlohmann::json files = nlohmann::json::object();
        for (const auto &[file, tags] : m_fileToTags) {
            nlohmann::json obj = nlohmann::json::object();
            nlohmann::json arr = nlohmann::json::array();
            for (const QString &tag : tags) {
                arr.push_back(tag.toStdString());
            }
            obj["tags"] = arr;
            const auto hit = m_pathToContentHash.find(file);
            if (hit != m_pathToContentHash.end() && !hit->second.isEmpty()) {
                obj["content_sha256"] = hit->second.toStdString();
            }
            files[file.toStdString()] = obj;
        }
        root["files"] = files;

        nlohmann::json cache = nlohmann::json::object();
        for (const auto &[h, j] : m_hashAnalysisCache) {
            cache[h.toStdString()] = j;
        }
        root["hash_analysis_cache"] = cache;

        nlohmann::json tp = nlohmann::json::object();
        for (const auto &[child, parent] : m_tagParents) {
            if (child.isEmpty() || parent.isEmpty()) continue;
            tp[child.toStdString()] = parent.toStdString();
        }
        root["tag_parents"] = tp;

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
    const QString nt = normalizeTag(tag);
    if (nt.isEmpty()) return;
    m_fileToTags[filename].insert(nt);
    m_tagToFilePaths[nt].insert(filename);
    if (save) {
        saveTags();
    }
}

void TagManager::removeTag(const QString& filename, const QString& tag) {
    QMutexLocker locker(&m_mutex);
    const QString nt = normalizeTag(tag);
    if (nt.isEmpty()) return;
    if (m_fileToTags.count(filename)) {
        m_fileToTags[filename].erase(nt);
        if (m_fileToTags[filename].empty()) m_fileToTags.erase(filename);
    }
    if (m_tagToFilePaths.count(nt)) {
        m_tagToFilePaths[nt].erase(filename);
        if (m_tagToFilePaths[nt].empty()) m_tagToFilePaths.erase(nt);
    }
    saveTags();
}

void TagManager::renameTag(const QString& oldTag, const QString& newTag) {
    QMutexLocker locker(&m_mutex);
    const QString nOld = normalizeTag(oldTag);
    const QString nNew = normalizeTag(newTag);
    if (nOld.isEmpty() || nNew.isEmpty()) return;
    std::map<QString, QString> newParents;
    for (const auto &pr : m_tagParents) {
        QString c = pr.first;
        QString p = pr.second;
        if (c == nOld) c = nNew;
        if (p == nOld) p = nNew;
        if (c.isEmpty() || p.isEmpty()) continue;
        newParents[c] = p;
    }
    m_tagParents = std::move(newParents);
    if (m_tagToFilePaths.count(nOld)) {
        std::set<QString> files = m_tagToFilePaths[nOld];
        m_tagToFilePaths.erase(nOld);
        for (const QString& file : files) {
            m_fileToTags[file].erase(nOld);
            m_fileToTags[file].insert(nNew);
            m_tagToFilePaths[nNew].insert(file);
        }
        saveTags();
    }
}

void TagManager::deleteTag(const QString& tag) {
    QMutexLocker locker(&m_mutex);
    const QString nt = normalizeTag(tag);
    if (nt.isEmpty()) return;
    for (auto it = m_tagParents.begin(); it != m_tagParents.end();) {
        if (it->first == nt || it->second == nt)
            it = m_tagParents.erase(it);
        else
            ++it;
    }
    if (m_tagToFilePaths.count(nt)) {
        std::set<QString> files = m_tagToFilePaths[nt];
        m_tagToFilePaths.erase(nt);
        for (const QString& file : files) {
            m_fileToTags[file].erase(nt);
            if (m_fileToTags[file].empty()) m_fileToTags.erase(file);
        }
        saveTags();
    }
}

void TagManager::mergeTag(const QString& oldTag, const QString& newTag) {
    QMutexLocker locker(&m_mutex);
    const QString nOld = normalizeTag(oldTag);
    const QString nNew = normalizeTag(newTag);
    if (nOld.isEmpty() || nNew.isEmpty()) return;
    if (nOld == nNew) return;

    auto it = m_tagToFilePaths.find(nOld);
    if (it == m_tagToFilePaths.end()) return;

    const std::set<QString> files = it->second; // copy
    for (const QString &fp : files) {
        // remove old
        if (m_fileToTags.count(fp)) {
            m_fileToTags[fp].erase(nOld);
            m_fileToTags[fp].insert(nNew);
        } else {
            m_fileToTags[fp].insert(nNew);
        }
        m_tagToFilePaths[nNew].insert(fp);
    }

    // remove old tag mapping entirely
    m_tagToFilePaths.erase(nOld);

    {
        std::map<QString, QString> newParents;
        for (const auto &pr : m_tagParents) {
            QString c = pr.first;
            QString p = pr.second;
            if (c == nOld) c = nNew;
            if (p == nOld) p = nNew;
            if (c.isEmpty() || p.isEmpty() || c == p) continue;
            newParents[c] = p;
        }
        m_tagParents = std::move(newParents);
    }

    // Remove from rejected tags if present (optional hygiene)
    if (m_rejectedTags.count(nOld)) {
        m_rejectedTags.erase(nOld);
    }

    saveTags();
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

    bool changed = false;
    auto it = m_fileToTags.find(oldPath);
    if (it != m_fileToTags.end()) {
        std::set<QString> tags = std::move(it->second);
        m_fileToTags.erase(it);
        m_fileToTags[newPath] = std::move(tags);

        for (const QString& t : m_fileToTags[newPath]) {
            m_tagToFilePaths[t].erase(oldPath);
            m_tagToFilePaths[t].insert(newPath);
        }
        changed = true;
    }

    {
        auto hIt = m_pathToContentHash.find(oldPath);
        if (hIt != m_pathToContentHash.end()) {
            m_pathToContentHash[newPath] = hIt->second;
            m_pathToContentHash.erase(hIt);
            changed = true;
        }
    }

    if (changed && saveMetadata) {
        saveTags();
    }
}

void TagManager::removeFileMetadata(const QString &path, bool save) {
    QMutexLocker locker(&m_mutex);
    m_pathToContentHash.erase(path);
    if (!m_fileToTags.count(path)) {
        if (save) saveTags();
        return;
    }
    for (const QString &t : m_fileToTags[path]) {
        m_tagToFilePaths[t].erase(path);
        if (m_tagToFilePaths[t].empty()) m_tagToFilePaths.erase(t);
    }
    m_fileToTags.erase(path);
    if (save) saveTags();
}

void TagManager::setFileContentHash(const QString &path, const QString &sha256Hex, bool save) {
    QMutexLocker locker(&m_mutex);
    if (path.isEmpty()) return;
    if (sha256Hex.isEmpty()) {
        m_pathToContentHash.erase(path);
    } else {
        m_pathToContentHash[path] = sha256Hex;
    }
    if (save) saveTags();
}

QString TagManager::fileContentHash(const QString &path) const {
    QMutexLocker locker(&m_mutex);
    const auto it = m_pathToContentHash.find(path);
    if (it == m_pathToContentHash.end()) return {};
    return it->second;
}

void TagManager::recordHashAnalysis(const QString &sha256Hex, const QJsonObject &analysis, bool save) {
    QMutexLocker locker(&m_mutex);
    if (sha256Hex.isEmpty()) return;
    const QByteArray raw = QJsonDocument(analysis).toJson(QJsonDocument::Compact);
    try {
        m_hashAnalysisCache[sha256Hex] = nlohmann::json::parse(std::string(raw.constData(), static_cast<size_t>(raw.size())));
    } catch (const std::exception &) {
        return;
    }
    if (save) saveTags();
}

bool TagManager::tryGetHashAnalysis(const QString &sha256Hex, QJsonObject *out) const {
    QMutexLocker locker(&m_mutex);
    if (!out || sha256Hex.isEmpty()) return false;
    const auto it = m_hashAnalysisCache.find(sha256Hex);
    if (it == m_hashAnalysisCache.end()) return false;
    QJsonParseError err{};
    const QByteArray raw = QByteArray::fromStdString(it->second.dump());
    const QJsonDocument d = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !d.isObject()) return false;
    *out = d.object();
    return true;
}

void TagManager::exportHashAnalysisCache(QHash<QString, QJsonObject> *dst) const {
    if (!dst) return;
    QMutexLocker locker(&m_mutex);
    dst->clear();
    for (const auto &[h, j] : m_hashAnalysisCache) {
        QJsonParseError err{};
        const QByteArray raw = QByteArray::fromStdString(j.dump());
        const QJsonDocument d = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !d.isObject()) continue;
        dst->insert(h, d.object());
    }
}

QStringList TagManager::filePathsWithFileName(const QString &baseFileName) const
{
    QMutexLocker locker(&m_mutex);
    QStringList out;
    if (baseFileName.isEmpty()) return out;
    for (const auto &entry : m_fileToTags) {
        if (QFileInfo(entry.first).fileName().compare(baseFileName, Qt::CaseInsensitive) == 0) {
            out << entry.first;
        }
    }
    out.removeDuplicates();
    return out;
}

QStringList TagManager::filePathsWithContentHash(const QString &sha256Hex) const
{
    QMutexLocker locker(&m_mutex);
    QStringList out;
    if (sha256Hex.isEmpty()) return out;
    for (const auto &[path, hx] : m_pathToContentHash) {
        if (hx.compare(sha256Hex, Qt::CaseInsensitive) == 0) out << path;
    }
    out.removeDuplicates();
    return out;
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
        const QString nt = normalizeTag(tag);
        if (nt.isEmpty()) continue;
        m_fileToTags[filename].insert(nt);
        m_tagToFilePaths[nt].insert(filename);
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
    const QString nt = normalizeTag(tag);
    if (nt.isEmpty()) return {};
    std::vector<QString> result;
    auto it = m_tagToFilePaths.find(nt);
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
            const QString t = normalizeTag(QString::fromStdString(v.get<std::string>())); // normalizes + trims
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
    const QString t = normalizeTag(tag);
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

void TagManager::clearAiTagsAndSummaries(bool save)
{
    QMutexLocker locker(&m_mutex);
    std::set<QString> allPaths;
    for (const auto &e : m_fileToTags) allPaths.insert(e.first);
    for (const auto &e : m_pathToContentHash) allPaths.insert(e.first);

    std::map<QString, std::set<QString>> newFileToTags;
    std::map<QString, std::set<QString>> newTagToFilePaths;

    for (const QString &path : allPaths) {
        std::set<QString> kept;
        const auto itOld = m_fileToTags.find(path);
        if (itOld != m_fileToTags.end()) {
            for (const QString &t : itOld->second) {
                if (hasAiPrefix(t)) continue;
                const QString nt = normalizeTag(t);
                if (nt.isEmpty()) continue;
                kept.insert(nt);
            }
        }
        if (!kept.empty()) {
            newFileToTags[path] = kept;
            for (const QString &t : kept) newTagToFilePaths[t].insert(path);
        } else if (m_pathToContentHash.count(path) || itOld != m_fileToTags.end()) {
            newFileToTags[path] = {};
        }
    }

    for (const auto &e : m_pathToContentHash) {
        if (!newFileToTags.count(e.first)) newFileToTags[e.first] = {};
    }

    m_fileToTags = std::move(newFileToTags);
    m_tagToFilePaths = std::move(newTagToFilePaths);
    m_hashAnalysisCache.clear();
    m_tagParents.clear();
    if (save) saveTags();
}

void TagManager::clearHashCaches(bool save)
{
    QMutexLocker locker(&m_mutex);
    m_pathToContentHash.clear();
    m_hashAnalysisCache.clear();
    if (save) saveTags();
}

void TagManager::factoryResetWorkspaceData()
{
    std::string metaPath;
    std::string rejPath;
    {
        QMutexLocker locker(&m_mutex);
        metaPath = getMetadataPath();
        rejPath = getRejectedTagsPath();
        m_tagToFilePaths.clear();
        m_fileToTags.clear();
        m_pathToContentHash.clear();
        m_hashAnalysisCache.clear();
        m_rejectedTags.clear();
        m_tagParents.clear();
    }
    try {
        if (!metaPath.empty() && fs::exists(metaPath)) fs::remove(metaPath);
        if (!rejPath.empty() && fs::exists(rejPath)) fs::remove(rejPath);
    } catch (const std::exception &e) {
        qDebug() << "TagManager::factoryResetWorkspaceData remove error:" << e.what();
    } catch (...) {
        qDebug() << "TagManager::factoryResetWorkspaceData remove unknown error";
    }
}

QString TagManager::tagParent(const QString &tag) const
{
    QMutexLocker locker(&m_mutex);
    const QString nt = normalizeTag(tag);
    const auto it = m_tagParents.find(nt);
    if (it == m_tagParents.end()) return QString();
    return it->second;
}

bool TagManager::setAiTagParent(const QString &childTag, const QString &parentTag, bool save)
{
    QMutexLocker locker(&m_mutex);
    const QString child = normalizeTag(childTag);
    const QString parent = normalizeTag(parentTag);
    if (child.isEmpty() || !hasAiPrefix(child)) return false;
    if (!parent.isEmpty()) {
        if (!hasAiPrefix(parent)) return false;
        if (parent == child) return false;
        QString walk = parent;
        QSet<QString> seen;
        while (!walk.isEmpty()) {
            if (walk == child) return false;
            if (seen.contains(walk)) break;
            seen.insert(walk);
            const auto it = m_tagParents.find(walk);
            walk = (it == m_tagParents.end()) ? QString() : it->second;
        }
    }
    if (parent.isEmpty())
        m_tagParents.erase(child);
    else
        m_tagParents[child] = parent;
    if (save) saveTags();
    return true;
}

std::vector<QString> TagManager::directChildTags(const QString &parentTag) const
{
    QMutexLocker locker(&m_mutex);
    const QString p = normalizeTag(parentTag);
    std::vector<QString> out;
    for (const auto &pr : m_tagParents) {
        if (pr.second == p) out.push_back(pr.first);
    }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });
    return out;
}

void TagManager::deleteTagDissolveChildren(const QString &tag, bool save)
{
    QMutexLocker locker(&m_mutex);
    const QString nt = normalizeTag(tag);
    if (nt.isEmpty()) return;
    for (auto it = m_tagParents.begin(); it != m_tagParents.end();) {
        if (it->second == nt || it->first == nt)
            it = m_tagParents.erase(it);
        else
            ++it;
    }
    if (m_tagToFilePaths.count(nt)) {
        const std::set<QString> files = m_tagToFilePaths[nt];
        m_tagToFilePaths.erase(nt);
        for (const QString &file : files) {
            if (m_fileToTags.count(file)) {
                m_fileToTags[file].erase(nt);
                if (m_fileToTags[file].empty()) m_fileToTags.erase(file);
            }
        }
    }
    if (save) saveTags();
}

void TagManager::deleteTagCascadeAi(const QString &tag, bool save)
{
    QMutexLocker locker(&m_mutex);
    const QString root = normalizeTag(tag);
    if (root.isEmpty() || !hasAiPrefix(root)) return;

    QSet<QString> all;
    QQueue<QString> q;
    all.insert(root);
    q.enqueue(root);
    while (!q.isEmpty()) {
        const QString u = q.dequeue();
        for (const auto &pr : m_tagParents) {
            if (pr.second == u && hasAiPrefix(pr.first) && !all.contains(pr.first)) {
                all.insert(pr.first);
                q.enqueue(pr.first);
            }
        }
    }

    for (auto it = m_tagParents.begin(); it != m_tagParents.end();) {
        if (all.contains(it->first) || all.contains(it->second))
            it = m_tagParents.erase(it);
        else
            ++it;
    }

    for (const QString &t : all) {
        if (!m_tagToFilePaths.count(t)) continue;
        const std::set<QString> files = m_tagToFilePaths[t];
        m_tagToFilePaths.erase(t);
        for (const QString &f : files) {
            if (m_fileToTags.count(f)) {
                m_fileToTags[f].erase(t);
                if (m_fileToTags[f].empty()) m_fileToTags.erase(f);
            }
        }
    }
    if (save) saveTags();
}

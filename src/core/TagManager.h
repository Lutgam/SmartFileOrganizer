#ifndef TAGMANAGER_H
#define TAGMANAGER_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>
#include <QString>
#include <QStringList>
#include <QRecursiveMutex>
#include <QJsonObject>
#include <QHash>
#include <nlohmann/json.hpp>

class TagManager {
public:
    TagManager();

    /// Removes a leading case-insensitive `[AI]` marker and following whitespace.
    /// Never uses fixed character indices (avoids corrupting CJK / surrogate pairs).
    static QString stripAiPrefix(const QString &tag);
    /// True when \a tag (after trim) begins with `[AI]` (case-insensitive).
    static bool hasAiPrefix(const QString &tag);

    /// Renormalize in-memory tag keys (e.g. legacy `[ 資料庫` → `資料庫`) and persist once if changed.
    void repairMalformedTagKeys();

    void loadTags(const std::string& directory);
    void saveTags();
    
    void addTag(const QString& filename, const QString& tag, bool save = true);
    void removeTag(const QString& filename, const QString& tag);
    void renameTag(const QString& oldTag, const QString& newTag);
    void deleteTag(const QString& tag);
    void mergeTag(const QString& oldTag, const QString& newTag, bool saveImmediately = true);
    std::vector<QString> getTags(const QString& filename) const;
    
    void setTags(const QString& filename, const std::vector<QString>& tags);

    /// Each tagged file once; primary tag = lexicographically first in std::set (stable, no extra structure).
    std::vector<std::pair<QString, QString>> taggedFilesWithPrimaryTag() const;
    /// Move all tag associations from oldPath to newPath (after QFile::rename).
    void relocateFilePath(const QString& oldPath, const QString& newPath, bool saveMetadata = true);

    /// Remove all tag rows and per-file hash for \a path (e.g. after deleting the file on disk).
    void removeFileMetadata(const QString& path, bool save = true);

    /// Persisted SHA-256 (hex) of file bytes for dedup across restarts.
    void setFileContentHash(const QString& path, const QString& sha256Hex, bool save = true);
    QString fileContentHash(const QString& path) const;

    /// Content-hash → last successful AI JSON { summary, tags[] }.
    void recordHashAnalysis(const QString& sha256Hex, const QJsonObject& analysis, bool save = true);
    bool tryGetHashAnalysis(const QString& sha256Hex, QJsonObject* out) const;
    void exportHashAnalysisCache(QHash<QString, QJsonObject>* dst) const;

    QStringList filePathsWithFileName(const QString &baseFileName) const;
    QStringList filePathsWithContentHash(const QString &sha256Hex) const;

    std::vector<QString> getAllTags() const;
    std::vector<QString> getFilesByTag(const QString& tag) const;

    QString tagParent(const QString &tag) const;
    bool setAiTagParent(const QString &childTag, const QString &parentTag, bool save = true);
    std::vector<QString> directChildTags(const QString &parentTag) const;
    void deleteTagDissolveChildren(const QString &tag, bool save = true);
    void deleteTagCascadeAi(const QString &tag, bool save = true);

    void addRejectedTag(const QString& tag);
    QStringList getRejectedTags() const;

    /// Clear all tags + hash-analysis JSON cache; keep per-file paths and content_sha256 map.
    void clearAiTagsAndSummaries(bool save = true);
    /// Clear persisted SHA-256 per file and hash_analysis_cache (forces re-hash / re-analysis).
    void clearHashCaches(bool save = true);
    /// In-memory wipe + delete metadata.json and rejected_tags.json on disk (does not recreate files).
    void factoryResetWorkspaceData();

private:
    std::string currentDirectory;
    std::string metadataFile;
    
    std::map<QString, std::set<QString>> m_tagToFilePaths;
    std::map<QString, std::set<QString>> m_fileToTags;

    std::set<QString> m_rejectedTags;
    std::map<QString, QString> m_pathToContentHash;
    std::map<QString, nlohmann::json> m_hashAnalysisCache;
    /// Child tag (canonical) → parent tag (canonical). Only meaningful edges are stored.
    std::map<QString, QString> m_tagParents;
    mutable QRecursiveMutex m_mutex;
    
    QString normalizeTag(const QString &tag) const;

    std::string getMetadataPath() const;
    std::string getRejectedTagsPath() const;
    void loadRejectedTags();
    void saveRejectedTags() const;
};

#endif // TAGMANAGER_H

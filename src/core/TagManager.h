#ifndef TAGMANAGER_H
#define TAGMANAGER_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <QString>
#include <nlohmann/json.hpp>

class TagManager {
public:
    TagManager();
    
    void loadTags(const std::string& directory);
    void saveTags();
    
    void addTag(const QString& filename, const QString& tag, bool save = true);
    void removeTag(const QString& filename, const QString& tag);
    void renameTag(const QString& oldTag, const QString& newTag);
    void deleteTag(const QString& tag);
    std::vector<QString> getTags(const QString& filename) const;
    
    void setTags(const QString& filename, const std::vector<QString>& tags);

    std::vector<QString> getAllTags() const;
    std::vector<QString> getFilesByTag(const QString& tag) const;

private:
    std::string currentDirectory;
    std::string metadataFile;
    
    std::map<QString, std::set<QString>> m_tagToFilePaths;
    std::map<QString, std::set<QString>> m_fileToTags;
    
    std::string getMetadataPath() const;
};

#endif // TAGMANAGER_H

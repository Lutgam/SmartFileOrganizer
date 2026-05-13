#ifndef DRAWERCATEGORYLUT_H
#define DRAWERCATEGORYLUT_H

#include <QMap>
#include <QString>
#include <QStringList>

/// Workspace-driven drawer LUT loaded from categories_config.json.
class SfDrawerCategoryLut {
public:
    static SfDrawerCategoryLut builtinDefault();
    static SfDrawerCategoryLut loadFromFile(const QString &absolutePath);
    static bool writeDefaultToFile(const QString &absolutePath);

    QStringList drawerKeys() const;
    const QMap<QString, QStringList> &keywordMap() const { return m_keywordsByDrawer; }

    QString matchText(const QString &text) const;
    QString normalizeDrawerKey(const QString &raw) const;
    bool isSyntheticDrawerFolderTag(const QString &tag) const;

private:
    QStringList m_drawerOrder;
    QMap<QString, QStringList> m_keywordsByDrawer;

    void rebuildDrawerOrderFromKeys();
};

void sfSetActiveDrawerCategoryLut(const SfDrawerCategoryLut &lut);
SfDrawerCategoryLut sfActiveDrawerCategoryLut();

#endif // DRAWERCATEGORYLUT_H

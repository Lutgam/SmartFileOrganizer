#ifndef AUTORULEENGINE_H
#define AUTORULEENGINE_H

#include <QString>
#include <QStringList>
#include <QVector>

/// One deterministic organizing rule: condition (folder / suffix / name
/// substring) → action (add tag, or move to a workspace subfolder).
struct SfAutoRule {
    enum class Action { AddTag, MoveToFolder };

    QString name;
    QString folder;        ///< absolute folder to watch; empty = whole workspace
    QString suffixCsv;     ///< e.g. "pdf,docx"; empty = any extension
    QString nameContains;  ///< case-insensitive filename substring; empty = any
    Action action = Action::AddTag;
    QString actionParam;   ///< tag text, or destination folder relative to workspace root
    bool enabled = true;
};

/// Pure rule persistence + matching. No file mutation here — the caller
/// previews matches and applies actions through its own (journaled) paths.
class SfAutoRuleEngine {
public:
    struct Match {
        QString filePath;
        int ruleIndex = -1;
    };

    static QVector<SfAutoRule> loadFromFile(const QString &absPath);
    static bool saveToFile(const QString &absPath, const QVector<SfAutoRule> &rules);

    /// True when the rule's condition matches this file path.
    static bool ruleMatchesFile(const SfAutoRule &rule, const QString &absFilePath);

    /// Deterministic scan: first matching enabled rule wins per file.
    static QVector<Match> findMatches(const QVector<SfAutoRule> &rules, const QString &workspaceRoot);
};

#endif // AUTORULEENGINE_H

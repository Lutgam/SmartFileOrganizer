#include "AutoRuleEngine.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
QString actionToString(SfAutoRule::Action a)
{
    return a == SfAutoRule::Action::MoveToFolder ? QStringLiteral("move") : QStringLiteral("tag");
}

SfAutoRule::Action actionFromString(const QString &s)
{
    return s == QStringLiteral("move") ? SfAutoRule::Action::MoveToFolder : SfAutoRule::Action::AddTag;
}
} // namespace

QVector<SfAutoRule> SfAutoRuleEngine::loadFromFile(const QString &absPath)
{
    QVector<SfAutoRule> out;
    QFile f(absPath);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue &v : root.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject o = v.toObject();
        SfAutoRule r;
        r.name = o.value(QStringLiteral("name")).toString();
        r.folder = o.value(QStringLiteral("folder")).toString();
        r.suffixCsv = o.value(QStringLiteral("suffixes")).toString();
        r.nameContains = o.value(QStringLiteral("name_contains")).toString();
        r.action = actionFromString(o.value(QStringLiteral("action")).toString());
        r.actionParam = o.value(QStringLiteral("action_param")).toString();
        r.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        if (!r.name.trimmed().isEmpty() && !r.actionParam.trimmed().isEmpty())
            out.append(r);
    }
    return out;
}

bool SfAutoRuleEngine::saveToFile(const QString &absPath, const QVector<SfAutoRule> &rules)
{
    QDir().mkpath(QFileInfo(absPath).absolutePath());
    QJsonArray arr;
    for (const SfAutoRule &r : rules) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), r.name);
        o.insert(QStringLiteral("folder"), r.folder);
        o.insert(QStringLiteral("suffixes"), r.suffixCsv);
        o.insert(QStringLiteral("name_contains"), r.nameContains);
        o.insert(QStringLiteral("action"), actionToString(r.action));
        o.insert(QStringLiteral("action_param"), r.actionParam);
        o.insert(QStringLiteral("enabled"), r.enabled);
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("rules"), arr);
    QFile f(absPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool SfAutoRuleEngine::ruleMatchesFile(const SfAutoRule &rule, const QString &absFilePath)
{
    if (!rule.enabled)
        return false;
    const QFileInfo fi(absFilePath);
    if (!rule.folder.trimmed().isEmpty()) {
        const QString want = QDir::cleanPath(rule.folder);
        const QString have = QDir::cleanPath(fi.absolutePath());
        if (have != want && !have.startsWith(want + QLatin1Char('/')))
            return false;
    }
    if (!rule.suffixCsv.trimmed().isEmpty()) {
        bool ok = false;
        const QString suffix = fi.suffix().toLower();
        for (const QString &s : rule.suffixCsv.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            if (s.trimmed().toLower() == suffix) {
                ok = true;
                break;
            }
        }
        if (!ok)
            return false;
    }
    if (!rule.nameContains.trimmed().isEmpty()
        && !fi.fileName().contains(rule.nameContains.trimmed(), Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

QVector<SfAutoRuleEngine::Match> SfAutoRuleEngine::findMatches(const QVector<SfAutoRule> &rules,
                                                               const QString &workspaceRoot)
{
    QVector<Match> out;
    const QString rootClean = QDir::cleanPath(workspaceRoot);
    if (rootClean.isEmpty() || rules.isEmpty())
        return out;

    QDirIterator it(rootClean, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = QDir::cleanPath(it.next());
        if (p.contains(QStringLiteral("/.smartfile")))
            continue;
        for (int i = 0; i < rules.size(); ++i) {
            if (!ruleMatchesFile(rules[i], p))
                continue;
            // Skip moves that are already in place.
            if (rules[i].action == SfAutoRule::Action::MoveToFolder) {
                const QString destDir =
                    QDir(rootClean).absoluteFilePath(rules[i].actionParam.trimmed());
                if (QDir::cleanPath(QFileInfo(p).absolutePath()) == QDir::cleanPath(destDir))
                    continue;
            }
            out.append({p, i});
            break; // first matching rule wins
        }
    }
    return out;
}

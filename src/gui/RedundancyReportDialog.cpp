#include "RedundancyReportDialog.h"
#include "LanguageManager.h"

#include <algorithm>

#include <QCheckBox>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QString shortHash(const QString &hex)
{
    if (hex.size() <= 16) return hex;
    return hex.left(8) + QStringLiteral("…") + hex.right(6);
}

QString displayPathStripDrawerEmoji(QString path)
{
    path = path.trimmed();
    if (path.isEmpty())
        return path;

    const bool useBackslash = path.contains(QLatin1Char('\\')) && !path.contains(QLatin1Char('/'));
    const QChar sep = useBackslash ? QLatin1Char('\\') : QLatin1Char('/');
    QStringList parts = path.split(sep, Qt::KeepEmptyParts);
    for (QString &part : parts) {
        if (part.isEmpty())
            continue;
        int i = 0;
        while (i < part.size()) {
            const QChar c = part.at(i);
            if (c.isSpace()) {
                ++i;
                continue;
            }
            if (c.isLetterOrNumber())
                break;
            ++i;
        }
        while (i < part.size() && part.at(i).isSpace())
            ++i;
        part = part.mid(i).trimmed();
    }
    return parts.join(sep);
}

void collectCheckedPathsFromGroup(QTreeWidgetItem *grp, QStringList *out)
{
    if (!grp || !out) return;
    QTreeWidget *tw = grp->treeWidget();
    if (!tw) return;
    for (int ci = 0; ci < grp->childCount(); ++ci) {
        QTreeWidgetItem *row = grp->child(ci);
        if (!row) continue;
        auto *cb = qobject_cast<QCheckBox *>(tw->itemWidget(row, 0));
        if (!cb || !cb->isChecked()) continue;
        const QString p = cb->property("absPath").toString();
        if (!p.isEmpty()) *out << p;
    }
}

} // namespace

RedundancyReportDialog::RedundancyReportDialog(QWidget *parent,
                                               int filesAnalyzed,
                                               int newTagAdds,
                                               int hashDuplicatePathCount,
                                               int nameConflictPathCount,
                                               const QMap<QString, QSet<QString>> &hashToPaths,
                                               const QMap<QString, QSet<QString>> &baseNameToPaths)
    : QDialog(parent)
{
    setWindowTitle(LanguageManager::instance().getText(QStringLiteral("redundancy_dialog_title")));

    auto *layout = new QVBoxLayout(this);
    auto *summary = new QLabel(
        LanguageManager::instance().getText(QStringLiteral("redundancy_dialog_body_grouped"))
            .arg(filesAnalyzed)
            .arg(newTagAdds)
            .arg(hashDuplicatePathCount)
            .arg(nameConflictPathCount),
        this);
    summary->setWordWrap(true);
    layout->addWidget(summary);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(1);
    m_tree->setHeaderHidden(true);
    m_tree->header()->setStretchLastSection(true);
    layout->addWidget(m_tree, 1);

    appendHashSection(hashToPaths);
    appendNameConflictSection(baseNameToPaths);

    auto *btnRow = new QHBoxLayout();
    auto *btnDelete = new QPushButton(
        LanguageManager::instance().getText(QStringLiteral("redundancy_execute_delete")),
        this);
    connect(btnDelete, &QPushButton::clicked, this, &RedundancyReportDialog::onExecuteDelete);
    btnRow->addWidget(btnDelete);
    btnRow->addStretch(1);
    auto *btnClose = new QPushButton(LanguageManager::instance().getText(QStringLiteral("dialog_close")), this);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(btnClose);
    layout->addLayout(btnRow);

    resize(820, 480);
}

void RedundancyReportDialog::appendHashSection(const QMap<QString, QSet<QString>> &hashToPaths)
{
    if (hashToPaths.isEmpty()) return;

    auto *section = new QTreeWidgetItem(m_tree, {LanguageManager::instance().getText(QStringLiteral("redundancy_section_hash"))});
    section->setExpanded(true);
    section->setFlags(section->flags() & ~Qt::ItemIsSelectable);

    for (auto it = hashToPaths.constBegin(); it != hashToPaths.constEnd(); ++it) {
        const QString hx = it.key();
        const QSet<QString> paths = it.value();
        if (paths.isEmpty()) continue;

        const QString title = LanguageManager::instance()
                                  .getText(QStringLiteral("redundancy_group_hash_title"))
                                  .arg(shortHash(hx))
                                  .arg(paths.size());
        auto *grp = new QTreeWidgetItem(section, {title});
        grp->setExpanded(true);
        grp->setFlags(grp->flags() & ~Qt::ItemIsSelectable);

        QStringList ordered;
        for (const QString &p : paths) ordered << p;
        std::sort(ordered.begin(), ordered.end(), [](const QString &a, const QString &b) {
            return a.localeAwareCompare(b) < 0;
        });

        for (const QString &path : ordered) {
            auto *row = new QTreeWidgetItem(grp, {QString()});
            auto *cb = new QCheckBox(displayPathStripDrawerEmoji(path), m_tree);
            cb->setProperty("absPath", path);
            cb->setChecked(false);
            m_tree->setItemWidget(row, 0, cb);
        }
    }
}

void RedundancyReportDialog::appendNameConflictSection(const QMap<QString, QSet<QString>> &baseNameToPaths)
{
    if (baseNameToPaths.isEmpty()) return;

    auto *section = new QTreeWidgetItem(m_tree, {LanguageManager::instance().getText(QStringLiteral("redundancy_section_name"))});
    section->setExpanded(true);
    section->setFlags(section->flags() & ~Qt::ItemIsSelectable);

    QStringList keys = baseNameToPaths.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });

    for (const QString &base : keys) {
        const QSet<QString> paths = baseNameToPaths.value(base);
        if (paths.size() < 2) continue;

        const QString title = LanguageManager::instance()
                                  .getText(QStringLiteral("redundancy_group_name_title"))
                                  .arg(base)
                                  .arg(paths.size());
        auto *grp = new QTreeWidgetItem(section, {title});
        grp->setExpanded(true);
        grp->setFlags(grp->flags() & ~Qt::ItemIsSelectable);

        QStringList ordered;
        for (const QString &p : paths) ordered << p;
        std::sort(ordered.begin(), ordered.end(), [](const QString &a, const QString &b) {
            return a.localeAwareCompare(b) < 0;
        });

        for (const QString &path : ordered) {
            auto *row = new QTreeWidgetItem(grp, {QString()});
            auto *cb = new QCheckBox(displayPathStripDrawerEmoji(path), m_tree);
            cb->setProperty("absPath", path);
            cb->setChecked(false);
            m_tree->setItemWidget(row, 0, cb);
        }
    }
}

void RedundancyReportDialog::onExecuteDelete()
{
    auto &lm = LanguageManager::instance();

    QStringList toDelete;
    for (int si = 0; si < m_tree->topLevelItemCount(); ++si) {
        QTreeWidgetItem *section = m_tree->topLevelItem(si);
        if (!section) continue;
        for (int gi = 0; gi < section->childCount(); ++gi) {
            QTreeWidgetItem *grp = section->child(gi);
            if (!grp) continue;
            collectCheckedPathsFromGroup(grp, &toDelete);
        }
    }

    if (toDelete.isEmpty()) {
        QMessageBox::warning(this,
                             lm.getText(QStringLiteral("redundancy_dialog_title")),
                             lm.getText(QStringLiteral("redundancy_delete_select_first")));
        return;
    }

    QStringList removed;
    for (const QString &p : toDelete) {
        if (QFile::remove(p)) removed << p;
    }

    if (!removed.isEmpty()) {
        QString bulletLines;
        for (const QString &p : removed) bulletLines += QStringLiteral("- %1\n").arg(p);
        bulletLines = bulletLines.trimmed();
        QMessageBox::information(this,
                                 lm.getText(QStringLiteral("redundancy_dialog_title")),
                                 lm.getText(QStringLiteral("redundancy_delete_success")).arg(bulletLines));
        emit redundantFilesRemoved(removed);
        accept();
        return;
    }

    QMessageBox::warning(this,
                         lm.getText(QStringLiteral("redundancy_dialog_title")),
                         lm.getText(QStringLiteral("redundancy_delete_none")));
}

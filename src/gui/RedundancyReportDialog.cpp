#include "RedundancyReportDialog.h"
#include "LanguageManager.h"

#include <algorithm>

#include <QButtonGroup>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QString shortHash(const QString &hex)
{
    if (hex.size() <= 16) return hex;
    return hex.left(8) + QStringLiteral("…") + hex.right(6);
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

        auto *bg = new QButtonGroup(this);
        m_buttonGroups.append(bg);

        QStringList ordered;
        for (const QString &p : paths) ordered << p;
        std::sort(ordered.begin(), ordered.end(), [](const QString &a, const QString &b) {
            return a.localeAwareCompare(b) < 0;
        });

        bool first = true;
        for (const QString &path : ordered) {
            auto *row = new QTreeWidgetItem(grp, {QString()});
            auto *rb = new QRadioButton(path);
            rb->setProperty("absPath", path);
            rb->setChecked(first);
            first = false;
            m_tree->setItemWidget(row, 0, rb);
            bg->addButton(rb);
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

        auto *bg = new QButtonGroup(this);
        m_buttonGroups.append(bg);

        QStringList ordered;
        for (const QString &p : paths) ordered << p;
        std::sort(ordered.begin(), ordered.end(), [](const QString &a, const QString &b) {
            return a.localeAwareCompare(b) < 0;
        });

        bool first = true;
        for (const QString &path : ordered) {
            auto *row = new QTreeWidgetItem(grp, {QString()});
            auto *rb = new QRadioButton(path);
            rb->setProperty("absPath", path);
            rb->setChecked(first);
            first = false;
            m_tree->setItemWidget(row, 0, rb);
            bg->addButton(rb);
        }
    }
}

void RedundancyReportDialog::onExecuteDelete()
{
    QStringList toDelete;
    for (int si = 0; si < m_tree->topLevelItemCount(); ++si) {
        QTreeWidgetItem *section = m_tree->topLevelItem(si);
        if (!section) continue;
        for (int gi = 0; gi < section->childCount(); ++gi) {
            QTreeWidgetItem *grp = section->child(gi);
            if (!grp) continue;

            QString keepPath;
            for (int ci = 0; ci < grp->childCount(); ++ci) {
                QTreeWidgetItem *row = grp->child(ci);
                if (!row) continue;
                auto *rb = qobject_cast<QRadioButton *>(m_tree->itemWidget(row, 0));
                if (!rb) continue;
                if (rb->isChecked()) {
                    keepPath = rb->property("absPath").toString();
                    break;
                }
            }
            if (keepPath.isEmpty() && grp->childCount() == 1) {
                auto *rb = qobject_cast<QRadioButton *>(m_tree->itemWidget(grp->child(0), 0));
                if (rb) keepPath = rb->property("absPath").toString();
            }

            for (int ci = 0; ci < grp->childCount(); ++ci) {
                QTreeWidgetItem *row = grp->child(ci);
                if (!row) continue;
                auto *rb = qobject_cast<QRadioButton *>(m_tree->itemWidget(row, 0));
                if (!rb) continue;
                const QString p = rb->property("absPath").toString();
                if (!p.isEmpty() && p != keepPath) toDelete << p;
            }
        }
    }

    QStringList removed;
    for (const QString &p : toDelete) {
        if (QFile::remove(p)) removed << p;
    }

    if (!removed.isEmpty()) {
        emit redundantFilesRemoved(removed);
        accept();
        return;
    }
    QMessageBox::warning(this,
                         LanguageManager::instance().getText(QStringLiteral("redundancy_dialog_title")),
                         LanguageManager::instance().getText(QStringLiteral("redundancy_delete_none")));
}

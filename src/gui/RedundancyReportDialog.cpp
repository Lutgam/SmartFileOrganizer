#include "RedundancyReportDialog.h"
#include "LanguageManager.h"

#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

RedundancyReportDialog::RedundancyReportDialog(QWidget *parent,
                                               int filesAnalyzed,
                                               int newTagAdds,
                                               int redundantCount,
                                               const QStringList &redundantPaths)
    : QDialog(parent)
{
    setWindowTitle(LanguageManager::instance().getText(QStringLiteral("redundancy_dialog_title")));

    auto *layout = new QVBoxLayout(this);
    auto *summary = new QLabel(
        LanguageManager::instance().getText(QStringLiteral("redundancy_dialog_body"))
            .arg(filesAnalyzed)
            .arg(newTagAdds)
            .arg(redundantCount),
        this);
    summary->setWordWrap(true);
    layout->addWidget(summary);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const QString &p : redundantPaths) {
        auto *item = new QListWidgetItem(p);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, p);
        m_list->addItem(item);
    }
    layout->addWidget(m_list, 1);

    auto *btnRow = new QHBoxLayout();
    auto *btnDelete = new QPushButton(
        LanguageManager::instance().getText(QStringLiteral("redundancy_delete_checked")),
        this);
    connect(btnDelete, &QPushButton::clicked, this, &RedundancyReportDialog::onDeleteChecked);
    btnRow->addWidget(btnDelete);
    btnRow->addStretch(1);
    auto *btnClose = new QPushButton(LanguageManager::instance().getText(QStringLiteral("dialog_close")), this);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(btnClose);
    layout->addLayout(btnRow);

    resize(720, 420);
}

void RedundancyReportDialog::onDeleteChecked()
{
    QStringList removed;
    for (int i = m_list->count() - 1; i >= 0; --i) {
        QListWidgetItem *item = m_list->item(i);
        if (!item || item->checkState() != Qt::Checked) continue;
        const QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty()) continue;
        if (QFile::remove(path)) {
            removed << path;
            delete m_list->takeItem(i);
        }
    }
    if (!removed.isEmpty()) {
        emit redundantFilesRemoved(removed);
        return;
    }
    QMessageBox::warning(this,
                         LanguageManager::instance().getText(QStringLiteral("redundancy_dialog_title")),
                         LanguageManager::instance().getText(QStringLiteral("redundancy_delete_none")));
}

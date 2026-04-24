#include "DuplicateCleanerDialog.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace {

struct QStringHash {
    std::size_t operator()(const QString &s) const noexcept {
        return static_cast<std::size_t>(qHash(s));
    }
};

struct QStringEq {
    bool operator()(const QString &a, const QString &b) const noexcept { return a == b; }
};

static QString humanSize(qint64 bytes) {
    const double b = static_cast<double>(bytes);
    if (b < 1024.0) return QStringLiteral("%1 B").arg(bytes);
    const double kb = b / 1024.0;
    if (kb < 1024.0) return QStringLiteral("%1 KB").arg(QString::number(kb, 'f', 1));
    const double mb = kb / 1024.0;
    if (mb < 1024.0) return QStringLiteral("%1 MB").arg(QString::number(mb, 'f', 1));
    const double gb = mb / 1024.0;
    return QStringLiteral("%1 GB").arg(QString::number(gb, 'f', 2));
}

} // namespace

DuplicateCleanerDialog::DuplicateCleanerDialog(const QString &targetPath, QWidget *parent)
    : QDialog(parent), m_targetPath(targetPath) {
    setWindowTitle(QStringLiteral("🧹 尋找同名檔"));
    resize(900, 600);

    auto *root = new QVBoxLayout(this);

    tree = new QTreeWidget(this);
    tree->setColumnCount(3);
    tree->setHeaderLabels({QStringLiteral("檔名群組"), QStringLiteral("檔案路徑"), QStringLiteral("大小")});
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    root->addWidget(tree, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    btnClose = new QPushButton(QStringLiteral("關閉"), this);
    btnRow->addWidget(btnClose);
    root->addLayout(btnRow);

    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    scanAndPopulate();
}

DuplicateCleanerDialog::~DuplicateCleanerDialog() = default;

void DuplicateCleanerDialog::scanAndPopulate() {
    tree->clear();
    if (m_targetPath.trimmed().isEmpty()) return;

    std::unordered_map<QString, std::vector<QString>, QStringHash, QStringEq> groups;
    groups.reserve(4096);

    QDirIterator it(m_targetPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);
        if (!fi.exists() || fi.isDir()) continue;
        const QString key = fi.completeBaseName();
        if (key.isEmpty()) continue;
        groups[key].push_back(fi.absoluteFilePath());
    }

    std::vector<QString> twinsKeys;
    twinsKeys.reserve(groups.size());

    for (const auto &kv : groups) {
        const auto &paths = kv.second;
        if (paths.size() < 2) continue;
        bool hasPdf = false;
        bool hasPpt = false;
        for (const QString &p : paths) {
            const QString suf = QFileInfo(p).suffix().toLower();
            if (suf == QStringLiteral("pdf")) hasPdf = true;
            if (suf == QStringLiteral("ppt") || suf == QStringLiteral("pptx")) hasPpt = true;
            if (hasPdf && hasPpt) break;
        }
        if (hasPdf && hasPpt) twinsKeys.push_back(kv.first);
    }

    std::sort(twinsKeys.begin(), twinsKeys.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });

    for (const QString &key : twinsKeys) {
        auto parent = new QTreeWidgetItem(tree);
        parent->setText(0, key);
        parent->setFirstColumnSpanned(true);

        auto paths = groups[key];
        std::sort(paths.begin(), paths.end(), [](const QString &a, const QString &b) {
            return a.localeAwareCompare(b) < 0;
        });

        for (const QString &p : paths) {
            const QFileInfo fi(p);
            auto child = new QTreeWidgetItem(parent);
            child->setText(0, fi.fileName());
            child->setText(1, fi.absoluteFilePath());
            child->setText(2, humanSize(fi.size()));
        }

        parent->setExpanded(true);
    }

    if (twinsKeys.empty()) {
        auto *empty = new QTreeWidgetItem(tree);
        empty->setText(0, QStringLiteral("未找到同時包含 PPT/PPTX 與 PDF 的同名檔案群組"));
        empty->setFirstColumnSpanned(true);
    }
}


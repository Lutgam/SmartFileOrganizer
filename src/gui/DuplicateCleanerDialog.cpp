#include "DuplicateCleanerDialog.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <QByteArrayView>

namespace {

static bool isInsideSmartfileMeta(const QString &rootClean, const QString &absFilePath) {
    const QString rel = QDir(rootClean).relativeFilePath(absFilePath);
    return rel == QStringLiteral(".smartfile") || rel.startsWith(QStringLiteral(".smartfile/"));
}

static QByteArray hashFileSha256(const QString &path, const std::atomic<bool> *cancel) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "DuplicateCleanerDialog: open failed" << path << f.errorString();
        return {};
    }

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    static constexpr qint64 CHUNK = 4 * 1024 * 1024;
    QByteArray buf;
    buf.resize(static_cast<int>(CHUNK));

    while (!f.atEnd()) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            return {};
        }
        const qint64 n = f.read(buf.data(), CHUNK);
        if (n < 0) {
            qDebug() << "DuplicateCleanerDialog: read failed" << path << f.errorString();
            return {};
        }
        if (n > 0) {
            hasher.addData(QByteArrayView(buf.constData(), static_cast<qsizetype>(n)));
        }
    }
    return hasher.result();
}

static QString uniqueDestinationPath(const QString &destDir, const QFileInfo &srcFi) {
    const QString base = srcFi.completeBaseName();
    const QString ext = srcFi.completeSuffix();
    const QString dotExt = ext.isEmpty() ? QString() : (QStringLiteral(".") + ext);

    QString candidate = QDir(destDir).absoluteFilePath(srcFi.fileName());
    if (!QFile::exists(candidate)) return candidate;

    for (int i = 1; i < 10000; ++i) {
        const QString name = QStringLiteral("%1 (%2)%3").arg(base).arg(i).arg(dotExt);
        candidate = QDir(destDir).absoluteFilePath(name);
        if (!QFile::exists(candidate)) return candidate;
    }

    const QString fallback = QStringLiteral("%1_%2%3")
                                 .arg(base)
                                 .arg(QDateTime::currentMSecsSinceEpoch())
                                 .arg(dotExt);
    return QDir(destDir).absoluteFilePath(fallback);
}

static QString pickKeeper(const QStringList &files) {
    // Choose "latest"; tie-breaker: shortest path.
    QString best;
    QDateTime bestTime;
    int bestLen = INT_MAX;
    for (const QString &p : files) {
        const QFileInfo fi(p);
        const QDateTime t = fi.lastModified();
        const int len = p.size();
        if (best.isEmpty() || t > bestTime || (t == bestTime && len < bestLen)) {
            best = p;
            bestTime = t;
            bestLen = len;
        }
    }
    return best;
}

} // namespace

DuplicateCleanerDialog::DuplicateCleanerDialog(const QString &targetPath, QWidget *parent)
    : QDialog(parent), m_targetPath(targetPath) {
    setWindowTitle(QStringLiteral("🧹 尋找冗餘檔案 (依 Hash)"));
    resize(980, 640);

    auto *root = new QVBoxLayout(this);

    tree = new QTreeWidget(this);
    tree->setColumnCount(2);
    tree->setHeaderLabels({QStringLiteral("重複檔案群組 (依 Hash)"), QStringLiteral("檔案路徑")});
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    root->addWidget(tree, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);

    btnMoveToStaging = new QPushButton(QStringLiteral("將勾選檔案移至待處理區"), this);
    btnMoveToStaging->setEnabled(false);
    btnRow->addWidget(btnMoveToStaging);

    btnCancel = new QPushButton(QStringLiteral("取消"), this);
    btnRow->addWidget(btnCancel);
    root->addLayout(btnRow);

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnMoveToStaging, &QPushButton::clicked, this, &DuplicateCleanerDialog::moveCheckedToStaging);
    connect(this, &QDialog::rejected, this, [this]() { m_cancelRequested.store(true, std::memory_order_relaxed); });

    startScan();
}

DuplicateCleanerDialog::~DuplicateCleanerDialog() {
    m_cancelRequested.store(true, std::memory_order_relaxed);
    if (watcher) {
        watcher->future().waitForFinished();
    }
}

QList<QPair<QString, QString>> DuplicateCleanerDialog::movedHistory() const {
    return m_movedHistory;
}

void DuplicateCleanerDialog::startScan() {
    tree->clear();
    m_movedHistory.clear();
    m_cancelRequested.store(false, std::memory_order_relaxed);

    const QString rootClean = QDir::cleanPath(m_targetPath);
    if (rootClean.isEmpty() || !QFileInfo(rootClean).exists()) {
        auto *empty = new QTreeWidgetItem(tree);
        empty->setText(0, QStringLiteral("請指定有效的資料夾"));
        empty->setFirstColumnSpanned(true);
        return;
    }

    progress = new QProgressDialog(QStringLiteral("正在掃描並計算 Hash，請稍候…"), QStringLiteral("取消"), 0, 1, this);
    progress->setWindowModality(Qt::ApplicationModal);
    progress->setMinimumDuration(0);
    progress->show();
    QCoreApplication::processEvents();
    connect(progress, &QProgressDialog::canceled, this, [this]() {
        m_cancelRequested.store(true, std::memory_order_relaxed);
    });

    watcher = new QFutureWatcher<QList<DuplicateGroup>>(this);
    connect(watcher, &QFutureWatcher<QList<DuplicateGroup>>::finished, this, [this]() {
        const QList<DuplicateGroup> groups = watcher->result();
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            tree->clear();
            auto *rootItem = new QTreeWidgetItem(tree);
            rootItem->setText(0, QStringLiteral("重複檔案群組 (依 Hash)"));
            rootItem->setFirstColumnSpanned(true);
            auto *msg = new QTreeWidgetItem(rootItem);
            msg->setText(0, QStringLiteral("已取消掃描"));
            msg->setFirstColumnSpanned(true);
            rootItem->setExpanded(true);
            btnMoveToStaging->setEnabled(false);
        } else {
            populateTree(groups);
        }
        if (progress) {
            progress->hide();
            progress->deleteLater();
            progress = nullptr;
        }
        watcher->deleteLater();
        watcher = nullptr;
    });

    const QPointer<DuplicateCleanerDialog> self(this);
    watcher->setFuture(QtConcurrent::run([self, rootClean]() -> QList<DuplicateGroup> {
        if (!self) return {};

        // 1) group by size
        QHash<qint64, QStringList> bySize;
        bySize.reserve(4096);

        QDirIterator it(rootClean, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (!self || self->m_cancelRequested.load(std::memory_order_relaxed)) return {};
            const QString p = it.next();
            const QFileInfo fi(p);
            if (!fi.exists() || !fi.isFile()) continue;
            const QString abs = fi.absoluteFilePath();
            if (isInsideSmartfileMeta(rootClean, abs)) continue;
            bySize[fi.size()].push_back(abs);
        }

        // Count how many files will be hashed (size groups with >=2)
        int totalToHash = 0;
        for (auto itSize = bySize.constBegin(); itSize != bySize.constEnd(); ++itSize) {
            const QStringList &paths = itSize.value();
            if (paths.size() < 2) continue;
            totalToHash += paths.size();
        }

        QMetaObject::invokeMethod(
            self,
            [self, totalToHash]() {
                if (!self || !self->progress) return;
                self->progress->setLabelText(QStringLiteral("正在計算 SHA-256…"));
                self->progress->setMaximum(std::max(1, totalToHash));
                self->progress->setValue(0);
            },
            Qt::QueuedConnection);

        // 2) for size groups > 1, group by hash
        QList<DuplicateGroup> out;
        int done = 0;

        for (auto itSize = bySize.constBegin(); itSize != bySize.constEnd(); ++itSize) {
            if (!self || self->m_cancelRequested.load(std::memory_order_relaxed)) return {};
            const QStringList &paths = itSize.value();
            if (paths.size() < 2) continue;

            QHash<QByteArray, QStringList> byHash;
            byHash.reserve(paths.size());

            for (const QString &p : paths) {
                if (!self || self->m_cancelRequested.load(std::memory_order_relaxed)) return {};

                const QByteArray h = hashFileSha256(p, &self->m_cancelRequested);
                ++done;
                QMetaObject::invokeMethod(
                    self,
                    [self, done]() {
                        if (!self || !self->progress) return;
                        self->progress->setValue(done);
                    },
                    Qt::QueuedConnection);

                if (h.isEmpty()) continue; // unreadable or cancelled -> skip
                byHash[h].push_back(p);
            }

            for (auto itH = byHash.constBegin(); itH != byHash.constEnd(); ++itH) {
                const QStringList &same = itH.value();
                if (same.size() < 2) continue;

                DuplicateGroup g;
                g.hashHex = QString::fromLatin1(itH.key().toHex());
                g.size = itSize.key();
                g.files = same;
                std::sort(g.files.begin(), g.files.end(), [](const QString &a, const QString &b) {
                    return a.localeAwareCompare(b) < 0;
                });
                out.push_back(std::move(g));
            }
        }

        std::sort(out.begin(), out.end(), [](const DuplicateGroup &a, const DuplicateGroup &b) {
            if (a.size != b.size) return a.size > b.size;
            return a.hashHex.localeAwareCompare(b.hashHex) < 0;
        });
        return out;
    }));
}

void DuplicateCleanerDialog::populateTree(const QList<DuplicateGroup> &groups) {
    tree->clear();

    auto *rootItem = new QTreeWidgetItem(tree);
    rootItem->setText(0, QStringLiteral("重複檔案群組 (依 Hash)"));
    rootItem->setFirstColumnSpanned(true);

    if (groups.isEmpty()) {
        auto *empty = new QTreeWidgetItem(rootItem);
        empty->setText(0, QStringLiteral("未找到重複檔案"));
        empty->setFirstColumnSpanned(true);
        rootItem->setExpanded(true);
        btnMoveToStaging->setEnabled(false);
        return;
    }

    for (const DuplicateGroup &g : groups) {
        auto *grp = new QTreeWidgetItem(rootItem);
        grp->setText(0, QStringLiteral("Hash %1 (%2)  大小: %3 bytes")
                            .arg(g.hashHex.left(12))
                            .arg(g.files.size())
                            .arg(g.size));
        grp->setFirstColumnSpanned(true);

        for (const QString &p : g.files) {
            auto *child = new QTreeWidgetItem(grp);
            child->setText(0, QFileInfo(p).fileName());
            child->setText(1, p);
            child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
            child->setCheckState(0, Qt::Unchecked);
        }
        grp->setExpanded(true);
    }

    rootItem->setExpanded(true);
    applyDefaultChecks();
    btnMoveToStaging->setEnabled(true);
}

void DuplicateCleanerDialog::applyDefaultChecks() {
    // Default: check all except one keeper per hash group (latest; tie shortest path)
    if (!tree || tree->topLevelItemCount() == 0) return;
    QTreeWidgetItem *rootItem = tree->topLevelItem(0);
    if (!rootItem) return;

    for (int i = 0; i < rootItem->childCount(); ++i) {
        QTreeWidgetItem *grp = rootItem->child(i);
        if (!grp) continue;

        QStringList files;
        files.reserve(grp->childCount());
        for (int j = 0; j < grp->childCount(); ++j) {
            QTreeWidgetItem *child = grp->child(j);
            if (!child) continue;
            files.push_back(child->text(1));
        }
        const QString keeper = pickKeeper(files);

        for (int j = 0; j < grp->childCount(); ++j) {
            QTreeWidgetItem *child = grp->child(j);
            if (!child) continue;
            const QString p = child->text(1);
            child->setCheckState(0, p == keeper ? Qt::Unchecked : Qt::Checked);
        }
    }
}

void DuplicateCleanerDialog::moveCheckedToStaging() {
    const QString rootClean = QDir::cleanPath(m_targetPath);
    if (rootClean.isEmpty()) return;

    const QString stagingDir = QDir(rootClean).absoluteFilePath(QStringLiteral("_冗餘檔案待處理區"));
    if (!QDir().mkpath(stagingDir)) {
        qDebug() << "DuplicateCleanerDialog: mkpath failed:" << stagingDir;
        return;
    }

    QList<QPair<QString, QString>> moved;

    if (!tree || tree->topLevelItemCount() == 0) return;
    QTreeWidgetItem *rootItem = tree->topLevelItem(0);
    if (!rootItem) return;

    for (int i = 0; i < rootItem->childCount(); ++i) {
        QTreeWidgetItem *grp = rootItem->child(i);
        if (!grp) continue;

        for (int j = 0; j < grp->childCount(); ++j) {
            QTreeWidgetItem *child = grp->child(j);
            if (!child) continue;
            if (child->checkState(0) != Qt::Checked) continue;

            const QString src = QDir::cleanPath(child->text(1));
            const QFileInfo fi(src);
            if (!fi.exists() || !fi.isFile()) {
                qDebug() << "DuplicateCleanerDialog: skip (missing):" << src;
                continue;
            }
            if (isInsideSmartfileMeta(rootClean, fi.absoluteFilePath())) {
                qDebug() << "DuplicateCleanerDialog: skip (.smartfile):" << src;
                continue;
            }

            const QString dest = uniqueDestinationPath(stagingDir, fi);
            QFile f(src);
            if (!f.rename(dest)) {
                qDebug() << "DuplicateCleanerDialog: rename failed" << src << "->" << dest << f.errorString();
                continue;
            }
            moved.push_back(qMakePair(src, dest)); // [old, new]
        }
    }

    m_movedHistory = moved;
    if (!m_movedHistory.isEmpty()) {
        accept(); // MainWindow will handle tagManager relocate + rescan
    } else {
        reject();
    }
}


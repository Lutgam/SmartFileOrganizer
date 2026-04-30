#include "DuplicateCleanerDialog.h"

#include <QByteArrayView>
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
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {

static bool isInsideSmartfileMeta(const QString &rootClean, const QString &absFilePath) {
    const QString rel = QDir(rootClean).relativeFilePath(absFilePath);
    return rel == QStringLiteral(".smartfile") || rel.startsWith(QStringLiteral(".smartfile/"));
}

static QByteArray hashFileSha256(const QString &path, const std::atomic<bool> *cancel) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "DuplicateCleanerWidget: open failed" << path << f.errorString();
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
            qDebug() << "DuplicateCleanerWidget: read failed" << path << f.errorString();
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

DuplicateCleanerWidget::DuplicateCleanerWidget(QWidget *parent) : QWidget(parent) {
    auto *root = new QVBoxLayout(this);

    m_pathLabel = new QLabel(QStringLiteral("掃描目錄：--"), this);
    m_pathLabel->setWordWrap(true);
    m_pathLabel->setStyleSheet(QStringLiteral("font-weight: 700;"));
    root->addWidget(m_pathLabel);

    auto *statusRow = new QHBoxLayout();
    m_statusLabel = new QLabel(QStringLiteral("狀態：就緒"), this);
    m_statusLabel->setWordWrap(true);
    statusRow->addWidget(m_statusLabel, 1);
    root->addLayout(statusRow);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_progressBar->setFormat(QStringLiteral("%p%"));
    root->addWidget(m_progressBar);

    tree = new QTreeWidget(this);
    tree->setColumnCount(2);
    tree->setHeaderLabels({QStringLiteral("重複檔案群組 (依 Hash)"), QStringLiteral("檔案路徑")});
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    root->addWidget(tree, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);

    btnStopScan = new QPushButton(QStringLiteral("停止掃描"), this);
    btnStopScan->setEnabled(false);
    btnRow->addWidget(btnStopScan);

    btnMoveToStaging = new QPushButton(QStringLiteral("將勾選檔案移至待處理區"), this);
    btnMoveToStaging->setEnabled(false);
    btnRow->addWidget(btnMoveToStaging);

    root->addLayout(btnRow);

    connect(btnStopScan, &QPushButton::clicked, this, &DuplicateCleanerWidget::requestStop);
    connect(btnMoveToStaging, &QPushButton::clicked, this, &DuplicateCleanerWidget::moveCheckedToStaging);
}

DuplicateCleanerWidget::~DuplicateCleanerWidget() {
    m_cancelRequested.store(true, std::memory_order_relaxed);
    if (watcher) {
        watcher->future().waitForFinished();
    }
}

void DuplicateCleanerWidget::startScanForPath(const QString &targetPath) {
    m_targetPath = targetPath;
    if (m_pathLabel) {
        m_pathLabel->setText(QStringLiteral("掃描目錄：%1").arg(QDir::cleanPath(m_targetPath)));
    }
    startScan();
}

void DuplicateCleanerWidget::requestStop() {
    m_cancelRequested.store(true, std::memory_order_relaxed);
    if (m_statusLabel) {
        m_statusLabel->setText(QStringLiteral("狀態：停止中…"));
    }
    if (btnStopScan) btnStopScan->setEnabled(false);
}

void DuplicateCleanerWidget::startScan() {
    tree->clear();
    m_movedHistory.clear();
    m_cancelRequested.store(false, std::memory_order_relaxed);

    if (btnMoveToStaging) btnMoveToStaging->setEnabled(false);
    if (btnStopScan) btnStopScan->setEnabled(true);
    if (m_progressBar) {
        m_progressBar->setRange(0, 1);
        m_progressBar->setValue(0);
    }
    if (m_statusLabel) m_statusLabel->setText(QStringLiteral("狀態：掃描檔案大小分群中…"));

    const QString rootClean = QDir::cleanPath(m_targetPath);
    if (rootClean.isEmpty() || !QFileInfo(rootClean).exists()) {
        auto *empty = new QTreeWidgetItem(tree);
        empty->setText(0, QStringLiteral("請指定有效的資料夾"));
        empty->setFirstColumnSpanned(true);
        if (btnStopScan) btnStopScan->setEnabled(false);
        return;
    }

    if (watcher) {
        // In case a previous run exists (shouldn't), wait and dispose.
        watcher->future().waitForFinished();
        watcher->deleteLater();
        watcher = nullptr;
    }

    watcher = new QFutureWatcher<QList<DuplicateGroup>>(this);
    connect(watcher, &QFutureWatcher<QList<DuplicateGroup>>::finished, this, [this]() {
        const QList<DuplicateGroup> groups = watcher->result();
        watcher->deleteLater();
        watcher = nullptr;

        if (btnStopScan) btnStopScan->setEnabled(false);

        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            tree->clear();
            auto *rootItem = new QTreeWidgetItem(tree);
            rootItem->setText(0, QStringLiteral("重複檔案群組 (依 Hash)"));
            rootItem->setFirstColumnSpanned(true);
            auto *msg = new QTreeWidgetItem(rootItem);
            msg->setText(0, QStringLiteral("已取消掃描"));
            msg->setFirstColumnSpanned(true);
            rootItem->setExpanded(true);
            if (m_statusLabel) m_statusLabel->setText(QStringLiteral("狀態：已取消"));
            if (btnMoveToStaging) btnMoveToStaging->setEnabled(false);
            return;
        }

        populateTree(groups);
        if (m_statusLabel) {
            m_statusLabel->setText(groups.isEmpty() ? QStringLiteral("狀態：未找到重複檔案")
                                                    : QStringLiteral("狀態：掃描完成，請勾選要移動的檔案"));
        }
    });

    const QPointer<DuplicateCleanerWidget> self(this);
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
                if (!self) return;
                if (self->m_statusLabel) self->m_statusLabel->setText(QStringLiteral("狀態：正在計算 SHA-256…"));
                if (self->m_progressBar) {
                    self->m_progressBar->setRange(0, std::max(1, totalToHash));
                    self->m_progressBar->setValue(0);
                }
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
                        if (!self || !self->m_progressBar) return;
                        self->m_progressBar->setValue(done);
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

void DuplicateCleanerWidget::populateTree(const QList<DuplicateGroup> &groups) {
    tree->clear();

    auto *rootItem = new QTreeWidgetItem(tree);
    rootItem->setText(0, QStringLiteral("重複檔案群組 (依 Hash)"));
    rootItem->setFirstColumnSpanned(true);

    if (groups.isEmpty()) {
        auto *empty = new QTreeWidgetItem(rootItem);
        empty->setText(0, QStringLiteral("未找到重複檔案"));
        empty->setFirstColumnSpanned(true);
        rootItem->setExpanded(true);
        if (btnMoveToStaging) btnMoveToStaging->setEnabled(false);
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
    if (btnMoveToStaging) btnMoveToStaging->setEnabled(true);
}

void DuplicateCleanerWidget::applyDefaultChecks() {
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

void DuplicateCleanerWidget::moveCheckedToStaging() {
    const QString rootClean = QDir::cleanPath(m_targetPath);
    if (rootClean.isEmpty()) return;

    const QString stagingDir = QDir(rootClean).absoluteFilePath(QStringLiteral("_冗餘檔案待處理區"));
    if (!QDir().mkpath(stagingDir)) {
        qDebug() << "DuplicateCleanerWidget: mkpath failed:" << stagingDir;
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
                qDebug() << "DuplicateCleanerWidget: skip (missing):" << src;
                continue;
            }
            if (isInsideSmartfileMeta(rootClean, fi.absoluteFilePath())) {
                qDebug() << "DuplicateCleanerWidget: skip (.smartfile):" << src;
                continue;
            }

            const QString dest = uniqueDestinationPath(stagingDir, fi);
            QFile f(src);
            if (!f.rename(dest)) {
                qDebug() << "DuplicateCleanerWidget: rename failed" << src << "->" << dest << f.errorString();
                continue;
            }
            moved.push_back(qMakePair(src, dest)); // [old, new]
        }
    }

    if (m_statusLabel) {
        m_statusLabel->setText(QStringLiteral("狀態：已搬移 %1 個檔案到待處理區").arg(moved.size()));
    }

    if (!moved.isEmpty()) {
        emit cleanupCompleted(moved);
    }
}


#include "MainWindow.h"
#include "../core/DocumentParser.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeDatabase>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QMutexLocker>
#include <QSizePolicy>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QThreadPool>
#include <QThread>

#include <algorithm>
#include <fstream>
#include <map>

#include "DuplicateCleanerDialog.h"
#include "GraphWidget.h"
#include "LanguageManager.h"
#include "SettingsDialog.h"

class FileItemDelegate : public QStyledItemDelegate {
public:
    explicit FileItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        // 【關鍵修復】：清空原本要交給系統預設畫筆的文字，徹底防止雙重渲染！
        opt.text = QString();

        // 1. 畫背景與選取狀態 (Highlight)
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        // 2. 取得資料
        QString name = index.data(Qt::DisplayRole).toString();
        QString path = index.data(Qt::UserRole + 1).toString();

        // 3. 準備畫筆設定
        QRect rect = opt.rect;
        rect.adjust(5, 0, -5, 0); // 左右邊距
        painter->save();

        // 4. 繪製檔名 (粗體，適應深淺色與選取狀態)
        QFont nameFont = opt.font;
        nameFont.setBold(true);
        painter->setFont(nameFont);

        if (opt.state & QStyle::State_Selected) {
            painter->setPen(opt.palette.highlightedText().color());
        } else {
            painter->setPen(opt.palette.text().color());
        }

        QFontMetrics fm(nameFont);
        QRect nameRect = fm.boundingRect(name);
        painter->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, name);

        // 5. 繪製灰色路徑 (在檔名後方)
        int pathX = rect.left() + nameRect.width() + 10; // 檔名後空 10px
        QRect pathRect = rect;
        pathRect.setLeft(pathX);

        painter->setFont(opt.font); // 恢復正常粗細
        if (!(opt.state & QStyle::State_Selected)) {
            painter->setPen(Qt::gray); // 未選取時維持灰色，選取時保持高亮色
        }
        painter->drawText(pathRect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("[%1]").arg(path));

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(200, 30);
    }
};

namespace {

static const QString kTagImage = QStringLiteral("🖼️ 圖片");
static const QString kTagVideo = QStringLiteral("🎬 影片");
static const QString kTagDoc = QStringLiteral("📄 文件");
static const QString kTagAudio = QStringLiteral("🎵 音檔");

static QString normalizeDisplayTag(const QString &t) {
    const QString s = t.trimmed();
    if (s == QStringLiteral("🖼️圖片") || s == kTagImage) return kTagImage;
    if (s == QStringLiteral("🎬影片") || s == kTagVideo) return kTagVideo;
    if (s == QStringLiteral("📄文件") || s == kTagDoc) return kTagDoc;
    if (s == QStringLiteral("🎧音訊") || s == QStringLiteral("🎵音檔") || s == kTagAudio) return kTagAudio;
    return s;
}

static QString systemTagBaseZh(const QString &canon) {
    if (canon == kTagImage) return QStringLiteral("圖片");
    if (canon == kTagVideo) return QStringLiteral("影片");
    if (canon == kTagDoc) return QStringLiteral("文件");
    if (canon == kTagAudio) return QStringLiteral("音檔");
    if (canon.contains(QStringLiteral("壓縮檔"))) return QStringLiteral("壓縮檔");
    if (canon.contains(QStringLiteral("程式碼"))) return QStringLiteral("程式碼");
    if (canon.contains(QStringLiteral("安裝檔"))) return QStringLiteral("安裝檔");
    if (canon.contains(QStringLiteral("備份檔"))) return QStringLiteral("備份檔");
    if (canon.contains(QStringLiteral("設定"))) return QStringLiteral("設定");
    if (canon.contains(QStringLiteral("設計"))) return QStringLiteral("設計");
    if (canon.contains(QStringLiteral("資料庫"))) return QStringLiteral("資料庫");
    if (canon.contains(QStringLiteral("學校作業"))) return QStringLiteral("學校作業");
    if (canon.contains(QStringLiteral("應用程式"))) return QStringLiteral("應用程式");
    if (canon.contains(QStringLiteral("履歷"))) return QStringLiteral("履歷");
    return QString();
}

static QString systemTagEmojiPrefix(const QString &canon) {
    // Extract a leading emoji-ish prefix even when there's no space.
    // We stop once we hit a letter/number or CJK ideograph.
    QString out;
    for (int i = 0; i < canon.size(); ++i) {
        const QChar c = canon.at(i);
        if (c.isSpace()) {
            if (!out.isEmpty()) break;
            continue;
        }
        const ushort u = c.unicode();
        const bool isCjk = (u >= 0x4E00 && u <= 0x9FFF);
        if (c.isLetterOrNumber() || isCjk) {
            break;
        }
        out.append(c);
        // Safety: don't let it grow unbounded
        if (out.size() >= 6) break;
    }
    return out.trimmed();
}

static QString emojiForMime(const QMimeType &mt) {
    const QString name = mt.name();
    if (name.startsWith("image/")) return QStringLiteral("🖼️");
    if (name.startsWith("video/")) return QStringLiteral("🎬");
    if (name.startsWith("audio/")) return QStringLiteral("🎧");
    if (name == QStringLiteral("application/pdf")) return QStringLiteral("📄");
    if (name.startsWith("text/")) return QStringLiteral("📝");
    if (name.contains(QStringLiteral("zip")) || name.contains(QStringLiteral("rar")) || name.contains(QStringLiteral("7z")) || name.contains(QStringLiteral("tar")))
        return QStringLiteral("📦");
    if (name.contains(QStringLiteral("json")) || name.contains(QStringLiteral("xml")) || name.contains(QStringLiteral("yaml")))
        return QStringLiteral("🧩");
    return QStringLiteral("📎");
}

static QString mimeDisplay(const QMimeType &mt) {
    const QString comment = mt.comment();
    if (!comment.isEmpty()) return comment;
    return mt.name();
}

static QString baseName(const QString &absPath) {
    return QFileInfo(absPath).fileName();
}

static QString parentDirDisplay(const QString &absPath) {
    return QFileInfo(absPath).absolutePath();
}

static QString resolveModelPath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString candidates[] = {
        QDir(appDir).filePath(QStringLiteral("assets/models/chat_model.gguf")),
        QDir(appDir).filePath(QStringLiteral("../assets/models/chat_model.gguf")),
    };
    for (const QString &p : candidates) {
        const QString clean = QDir::cleanPath(p);
        if (QFile::exists(clean)) return clean;
    }
    return QDir::cleanPath(QDir(appDir).filePath(QStringLiteral("assets/models/chat_model.gguf")));
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupToolbar();
    m_mainTabWidget = new QTabWidget(this);
    setCentralWidget(m_mainTabWidget);

    m_workspaceTab = new QWidget(this);
    auto *workspaceLayout = new QVBoxLayout(m_workspaceTab);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);

    setupFourColumnLayout();
    workspaceLayout->addWidget(mainSplitter);
    m_mainTabWidget->addTab(m_workspaceTab, tr("核心工作區"));

    m_duplicateCleanerTab = new QWidget(this);
    auto *dupLayout = new QVBoxLayout(m_duplicateCleanerTab);
    dupLayout->setContentsMargins(10, 10, 10, 10);
    m_duplicateCleanerWidget = new DuplicateCleanerWidget(m_duplicateCleanerTab);
    dupLayout->addWidget(m_duplicateCleanerWidget, 1);
    connect(m_duplicateCleanerWidget, &DuplicateCleanerWidget::cleanupCompleted,
            this, &MainWindow::onDuplicateCleanupCompleted);
    m_mainTabWidget->addTab(m_duplicateCleanerTab, tr("冗餘檔案清理"));

    m_graphTab = new QWidget(this);
    auto *graphLayout = new QVBoxLayout(m_graphTab);
    graphLayout->setContentsMargins(0, 0, 0, 0);
    m_graphWidget = new GraphWidget(&tagManager, m_graphTab);
    graphLayout->addWidget(m_graphWidget, 1);
    m_mainTabWidget->addTab(m_graphTab, tr("關聯圖譜分析"));

    connect(m_mainTabWidget, &QTabWidget::currentChanged, this, [this](int) {
        if (!m_mainTabWidget || !m_graphWidget || !m_graphTab) return;
        if (m_mainTabWidget->currentWidget() == m_graphTab) {
            m_graphWidget->buildGraph();
        }
    });

    setupContextMenus();

    m_dirWatcher = new QFileSystemWatcher(this);
    connect(m_dirWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::onDirectoryChanged);
    m_dirDebounceTimer = new QTimer(this);
    m_dirDebounceTimer->setSingleShot(true);
    m_dirDebounceTimer->setInterval(1000);
    connect(m_dirDebounceTimer, &QTimer::timeout, this, [this]() {
        // If AI/background scan is running, skip notification to avoid disruption.
        const bool busy = (watcher && watcher->isRunning())
                          || (initialScanWatcher && initialScanWatcher->isRunning())
                          || (modelLoadWatcher && modelLoadWatcher->isRunning());
        if (busy) return;
        if (rootPath.trimmed().isEmpty()) return;

        const int answer = QMessageBox::question(
            this,
            tr("Workspace changed"),
            tr("Detected workspace file changes. Rescan and refresh?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            scanFiles();
            updateTagList();
        }
    });

    // 保留至少一個核心給 UI 執行緒
    int idealThreads = QThread::idealThreadCount();
    int maxThreads = qMax(1, idealThreads - 1); // 如果單核就維持 1，多核則減 1
    QThreadPool::globalInstance()->setMaxThreadCount(maxThreads);

    watcher = new QFutureWatcher<std::string>(this);
    connect(watcher, &QFutureWatcher<std::string>::finished, this, &MainWindow::onAnalysisFinished);

    modelLoadWatcher = new QFutureWatcher<bool>(this);
    connect(modelLoadWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        const bool ok = modelLoadWatcher->result();
        lblStatus->setText(ok ? LanguageManager::instance().getText(QStringLiteral("模型已自動載入 (Model auto-loaded)"))
                              : LanguageManager::instance().getText(QStringLiteral("模型自動載入失敗 (Auto-load failed)")));
    });

    initialScanWatcher = new QFutureWatcher<void>(this);
    connect(initialScanWatcher, &QFutureWatcher<void>::finished, this, &MainWindow::onBackgroundScanFinished);

    llamaEngine.setCancelFlag(&cancelFlag);

    mapsHomeFixAndSetRoot(QDir::homePath());
    navHistory.clear();
    navIndex = -1;
    pushHistory(currentPath);
    fileListMode = FileListMode::PhysicalFolder;
    activeVirtualTag.clear();
    scanFiles();

    const QString modelPath = resolveModelPath();
    if (!QFile::exists(modelPath)) {
        lblStatus->setText(LanguageManager::instance()
                               .getText(QStringLiteral("❌ 找不到模型: %1（請確認 assets/models/chat_model.gguf）"))
                               .arg(modelPath));
    } else {
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("正在自動載入模型… %1")).arg(modelPath));
        modelLoadWatcher->setFuture(QtConcurrent::run([this, modelPath]() {
            return llamaEngine.loadModel(modelPath.toStdString());
        }));
    }

    initialScanWatcher->setFuture(QtConcurrent::run([this]() {
        const QString baseDir = rootPath.isEmpty() ? QDir::homePath() : rootPath;
        int n = 0;
        QDirIterator it(baseDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString filePath = it.next();
            const QFileInfo fileInfo(filePath);
            if (!fileInfo.exists()) continue;
            if (fileInfo.isDir()) continue;
            if (fileInfo.isSymLink()) {
                const QString target = fileInfo.symLinkTarget();
                if (!target.isEmpty() && QFileInfo(target).isDir()) continue;
            }

            const QString fileName = fileInfo.fileName();
            const QStringList fastTags = getFastPathTags(fileName);
            if (fastTags.isEmpty()) {
                ++n;
                if ((n % 2000) == 0) {
                    QMetaObject::invokeMethod(
                        this,
                        [this]() { onBackgroundScanProgress(); },
                        Qt::QueuedConnection);
                }
                continue;
            }

            {
                QMutexLocker locker(&tagMutex);
                const auto existingByPath = tagManager.getTags(filePath);
                const auto existingByName = tagManager.getTags(fileName);
                QSet<QString> existingSet;
                for (const auto &t : existingByPath) existingSet.insert(t);
                for (const auto &t : existingByName) existingSet.insert(t);
                for (const QString &t : fastTags) {
                    if (existingSet.contains(t)) continue;
                    tagManager.addTag(filePath, t, false);
                    existingSet.insert(t);
                }
            }
            ++n;
            if ((n % 2000) == 0) {
                QMetaObject::invokeMethod(
                    this,
                    [this]() { onBackgroundScanProgress(); },
                    Qt::QueuedConnection);
            }
        }
        {
            QMutexLocker locker(&tagMutex);
            tagManager.saveTags();
        }
    }));

    resize(1200, 800);
    setWindowTitle(QStringLiteral("Smart File Organizer"));

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() { updateAllTexts(); });
    updateAllTexts();
}

void MainWindow::onDirectoryChanged(const QString &path) {
    const QString clean = QDir::cleanPath(path);
    if (clean.contains(QStringLiteral("/.smartfile")) || clean.contains(QStringLiteral("\\.smartfile"))
        || clean.contains(QStringLiteral("_冗餘檔案待處理區"))) {
        return;
    }
    m_lastDirChangePath = path;
    if (m_dirDebounceTimer) {
        m_dirDebounceTimer->start();
    } else {
        QTimer::singleShot(1000, this, [this]() {
            if (rootPath.trimmed().isEmpty()) return;
            scanFiles();
            updateTagList();
        });
    }
}

void MainWindow::showDuplicateCleanerTab() {
    if (!m_mainTabWidget || !m_duplicateCleanerWidget) return;
    if (rootPath.trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("尋找冗餘檔案"), QStringLiteral("請先開啟工作區資料夾。"));
        return;
    }
    m_mainTabWidget->setCurrentWidget(m_duplicateCleanerTab);
    m_duplicateCleanerWidget->setSuggestedPath(rootPath);
}

void MainWindow::onDuplicateCleanupCompleted(const QList<QPair<QString, QString>> &movedHistory) {
    if (!movedHistory.isEmpty()) {
        QMutexLocker locker(&tagMutex);
        for (const auto &p : movedHistory) {
            tagManager.relocateFilePath(p.first, p.second, false);
        }
        tagManager.saveTags();
    }

    scanFiles();
    updateTagList();
    if (m_mainTabWidget && m_workspaceTab) {
        m_mainTabWidget->setCurrentWidget(m_workspaceTab);
    }
}

void MainWindow::openSettings() {
    SettingsDialog dlg(rootPath, this);
    connect(&dlg, &SettingsDialog::settingsApplied, this, [this]() { updateAllTexts(); });
    const int code = dlg.exec();
    if (code != QDialog::Accepted) return;

    updateAllTexts();

    const QString newModelPath = dlg.modelPath();
    if (!newModelPath.isEmpty()) {
        // Prevent reloading while inference is active.
        if (watcher && watcher->isRunning()) {
            cancelFlag.store(true);
            watcher->future().waitForFinished();
        }
        if (modelLoadWatcher && modelLoadWatcher->isRunning()) {
            modelLoadWatcher->future().waitForFinished();
        }

        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("正在載入新模型… %1")).arg(newModelPath));
        modelLoadWatcher->setFuture(QtConcurrent::run([this, newModelPath]() {
            return llamaEngine.loadModel(newModelPath.toStdString());
        }));
    }
}

void MainWindow::updateAllTexts() {
    auto &lm = LanguageManager::instance();

    if (m_mainTabWidget && m_workspaceTab) m_mainTabWidget->setTabText(m_mainTabWidget->indexOf(m_workspaceTab), lm.getText(QStringLiteral("tab_workspace")));
    if (m_mainTabWidget && m_duplicateCleanerTab) m_mainTabWidget->setTabText(m_mainTabWidget->indexOf(m_duplicateCleanerTab), lm.getText(QStringLiteral("tab_duplicates")));
    if (m_mainTabWidget && m_graphTab) m_mainTabWidget->setTabText(m_mainTabWidget->indexOf(m_graphTab), lm.getText(QStringLiteral("tab_graph")));

    if (m_actOpenFolder) m_actOpenFolder->setText(lm.getText(QStringLiteral("toolbar_open")));
    if (m_actFindDuplicates) m_actFindDuplicates->setText(lm.getText(QStringLiteral("toolbar_duplicates")));
    if (m_actSettings) m_actSettings->setText(lm.getText(QStringLiteral("toolbar_settings")));

    llamaEngine.setOutputLanguage(lm.language() == LanguageManager::Language::EN_US ? QStringLiteral("en_US")
                                                                                   : QStringLiteral("zh_TW"));

    if (btnAnalyzeFile) btnAnalyzeFile->setText(lm.getText(QStringLiteral("btn_analyze")));
    if (btnCancelAnalysis) btnCancelAnalysis->setText(lm.getText(QStringLiteral("btn_cancel")));
    if (btnSaveTags) btnSaveTags->setText(lm.getText(QStringLiteral("btn_save")));
    if (btnAddTag) btnAddTag->setText(lm.getText(QStringLiteral("btn_add_tag")));
    if (btnRemoveTag) btnRemoveTag->setText(lm.getText(QStringLiteral("btn_remove_tag")));
    if (btnAddExistingTag) btnAddExistingTag->setText(lm.getText(QStringLiteral("btn_add_existing_tag")));
    if (btnPhysicalArchive) btnPhysicalArchive->setText(lm.getText(QStringLiteral("btn_physical_archive")));
    if (btnUndoPhysicalArchive) btnUndoPhysicalArchive->setText(lm.getText(QStringLiteral("btn_undo_archive")));

    // Workspace static texts (labels, group titles, placeholders)
    if (lblTagLibraryTitle) lblTagLibraryTitle->setText(QStringLiteral("🏷️ %1").arg(lm.getText(QStringLiteral("標籤庫"))));
    if (chkRecursive) chkRecursive->setText(lm.getText(QStringLiteral("包含子資料夾")));
    if (lblFolderTreeTitle) lblFolderTreeTitle->setText(QStringLiteral("🗂️ %1").arg(lm.getText(QStringLiteral("資料夾樹"))));
    if (lblFileListTitle) lblFileListTitle->setText(QStringLiteral("📂 %1").arg(lm.getText(QStringLiteral("檔案清單"))));
    if (lblPreviewTitle) lblPreviewTitle->setText(QStringLiteral("👁️ %1").arg(lm.getText(QStringLiteral("預覽與控制"))));
    if (lblPreviewImage) {
        // Only update the default placeholder text
        if (lblPreviewImage->text().contains(QStringLiteral("選擇檔案以預覽"))
            || lblPreviewImage->text().contains(QStringLiteral("Select a file to preview"))) {
            lblPreviewImage->setText(lm.getText(QStringLiteral("選擇檔案以預覽")));
        }
    }
    if (grpTagManagement) grpTagManagement->setTitle(lm.getText(QStringLiteral("標籤管理")));
    if (grpFileOperations) grpFileOperations->setTitle(lm.getText(QStringLiteral("檔案操作")));
    if (btnRenameFile) btnRenameFile->setText(lm.getText(QStringLiteral("重新命名")));
    if (btnDeleteFile) btnDeleteFile->setText(lm.getText(QStringLiteral("刪除檔案")));
    if (btnRevealFile) btnRevealFile->setText(lm.getText(QStringLiteral("開啟位置")));

    if (cmbSort) {
        const int idx = cmbSort->currentIndex();
        cmbSort->blockSignals(true);
        cmbSort->clear();
        cmbSort->addItem(lm.getText(QStringLiteral("依名稱")));
        cmbSort->addItem(lm.getText(QStringLiteral("依日期")));
        cmbSort->addItem(lm.getText(QStringLiteral("依大小")));
        cmbSort->setCurrentIndex(std::max(0, idx));
        cmbSort->blockSignals(false);
    }
    if (txtSearch) {
        txtSearch->setPlaceholderText(QStringLiteral("🔍 %1").arg(lm.getText(QStringLiteral("搜尋"))));
    }

    // Home title (only when currently in Home mode)
    if (workspaceTitleLabel) {
        const QString homeTitleZh = QStringLiteral("📁 本機磁碟 (Home)");
        const QString homeTitleEn = QStringLiteral("📁 Local Disk (Home)");
        if (workspaceTitleLabel->text() == homeTitleZh || workspaceTitleLabel->text() == homeTitleEn) {
            workspaceTitleLabel->setText(QStringLiteral("📁 %1").arg(lm.getText(QStringLiteral("本機磁碟 (Home)"))));
        }
    }

    // Re-render tag list display names (system tags need presentation translation)
    if (tagListWidget) {
        const QString allFilesText = lm.getText(QStringLiteral("All Files"));
        if (tagListWidget->count() > 0) {
            auto *it0 = tagListWidget->item(0);
            if (it0 && it0->data(Qt::UserRole).toString() == QStringLiteral("ALL")) {
                it0->setText(allFilesText);
            }
        }
        for (int i = 0; i < tagListWidget->count(); ++i) {
            auto *it = tagListWidget->item(i);
            if (!it) continue;
            const QString role = it->data(Qt::UserRole).toString();
            if (role == QStringLiteral("ALL")) continue;
            const int n = it->data(Qt::UserRole + 1).toInt();
            const QString canon = normalizeDisplayTag(role);
            const QString baseZh = it->data(Qt::UserRole + 2).toString();
            const QString emoji = systemTagEmojiPrefix(canon);
            const QString displayName = baseZh.isEmpty()
                                            ? canon
                                            : QStringLiteral("%1 %2").arg(emoji, lm.getText(baseZh));
            it->setText(QStringLiteral("%1 (%2)").arg(displayName.trimmed()).arg(n));
        }
    }

    if (cmbTagFilter && cmbTagFilter->count() > 0) {
        cmbTagFilter->setItemText(0, lm.getText(QStringLiteral("All Files")));
    }

    if (lblTags) {
        const QString zh = QStringLiteral("標籤: --");
        const QString en = QStringLiteral("Tags: --");
        if (lblTags->text() == zh || lblTags->text() == en) {
            lblTags->setText(QStringLiteral("%1: --").arg(lm.getText(QStringLiteral("標籤"))));
        }
    }
    if (lblStatus) {
        const QString zh = QStringLiteral("狀態: 就緒");
        const QString en = QStringLiteral("Status: Ready");
        if (lblStatus->text() == zh || lblStatus->text() == en) {
            lblStatus->setText(QStringLiteral("%1: %2").arg(lm.getText(QStringLiteral("狀態")), lm.getText(QStringLiteral("就緒"))));
        }
    }

    if (m_duplicateCleanerWidget) m_duplicateCleanerWidget->updateTexts();

    if (btnLoadMore) {
        btnLoadMore->setText(QStringLiteral("%1 (%2)")
                                 .arg(lm.getText(QStringLiteral("載入更多")))
                                 .arg(BATCH_SIZE));
    }
    if (btnLoadAll) {
        btnLoadAll->setText(lm.getText(QStringLiteral("載入全部")));
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::onBackgroundScanProgress() {
    updateTagListCountsOnly();
}

void MainWindow::onBackgroundScanFinished() {
    updateTagList();
    lblStatus->setText(lblStatus->text() + QStringLiteral(" | %1").arg(LanguageManager::instance().getText(QStringLiteral("背景全域掃描完成"))));
}

void MainWindow::setupToolbar() {
    toolbar = addToolBar(tr("Main Toolbar"));
    toolbar->setMovable(false);
    m_actOpenFolder = toolbar->addAction(QStringLiteral("開啟資料夾"));
    connect(m_actOpenFolder, &QAction::triggered, this, &MainWindow::openFolder);

    m_actFindDuplicates = toolbar->addAction(QStringLiteral("🧹 尋找冗餘檔案"));
    connect(m_actFindDuplicates, &QAction::triggered, this, [this]() {
        showDuplicateCleanerTab();
    });

    toolbar->addSeparator();
    m_actSettings = toolbar->addAction(QStringLiteral("⚙️ 設定 (Settings)"));
    connect(m_actSettings, &QAction::triggered, this, &MainWindow::openSettings);
    toolbar->addSeparator();
}

void MainWindow::setupFourColumnLayout() {
    mainSplitter = new QSplitter(Qt::Horizontal, this);

    // --- Column 1: Tags ---
    tagsPanel = new QWidget(this);
    auto *tagsLayout = new QVBoxLayout(tagsPanel);
    auto *tagsHeader = new QHBoxLayout();
    lblTagLibraryTitle = new QLabel(QStringLiteral("🏷️ 標籤庫"), this);
    tagsHeader->addWidget(lblTagLibraryTitle);
    tagsHeader->addStretch(1);
    chkRecursive = new QCheckBox(QStringLiteral("包含子資料夾"), this);
    chkRecursive->setChecked(false);
    connect(chkRecursive, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) {
        if (fileListMode == FileListMode::PhysicalFolder) {
            scanFiles();
            sortFileList();
        }
    });
    tagsHeader->addWidget(chkRecursive);
    tagsLayout->addLayout(tagsHeader);

    tagListWidget = new QListWidget(this);
    tagListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tagListWidget, &QListWidget::itemClicked, this, &MainWindow::onTagSelected);
    tagsLayout->addWidget(tagListWidget);

    auto *tagButtons = new QHBoxLayout();
    btnLeftAddTag = new QPushButton(QStringLiteral("➕"), this);
    btnLeftAddTag->setToolTip(QStringLiteral("新增標籤"));
    connect(btnLeftAddTag, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString t = QInputDialog::getText(this, QStringLiteral("Add Tag"), QStringLiteral("New tag:"), QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || t.isEmpty()) return;
        const QString fp = currentFilePath();
        if (!fp.isEmpty()) {
            QMutexLocker locker(&tagMutex);
            tagManager.addTag(fp, t, true);
            tagManager.saveTags();
        }
        updateTagList();
        if (fileListMode == FileListMode::PhysicalFolder) scanFiles();
        else populateVirtualTagFiles(activeVirtualTag);
    });
    tagButtons->addWidget(btnLeftAddTag);

    btnLeftRemoveTag = new QPushButton(QStringLiteral("➖"), this);
    btnLeftRemoveTag->setToolTip(QStringLiteral("刪除標籤（全域）"));
    connect(btnLeftRemoveTag, &QPushButton::clicked, this, &MainWindow::removeGlobalTag);
    tagButtons->addWidget(btnLeftRemoveTag);
    tagsLayout->addLayout(tagButtons);

    mainSplitter->addWidget(tagsPanel);

    // --- Column 2: Folders + nav under title ---
    foldersPanel = new QWidget(this);
    auto *foldersLayout = new QVBoxLayout(foldersPanel);
    lblFolderTreeTitle = new QLabel(QStringLiteral("🗂️ 資料夾樹"), this);
    foldersLayout->addWidget(lblFolderTreeTitle);

    auto *navRow = new QHBoxLayout();
    btnBack = new QPushButton(QStringLiteral("⬅️"), this);
    btnBack->setToolTip(QStringLiteral("上一頁"));
    connect(btnBack, &QPushButton::clicked, this, &MainWindow::goBack);
    navRow->addWidget(btnBack);

    btnForward = new QPushButton(QStringLiteral("➡️"), this);
    btnForward->setToolTip(QStringLiteral("下一頁"));
    connect(btnForward, &QPushButton::clicked, this, &MainWindow::goForward);
    navRow->addWidget(btnForward);

    btnHome = new QPushButton(QStringLiteral("🏠"), this);
    btnHome->setToolTip(QStringLiteral("回首頁（家目錄）"));
    connect(btnHome, &QPushButton::clicked, this, &MainWindow::goHome);
    navRow->addWidget(btnHome);

    navRow->addStretch(1);
    foldersLayout->addLayout(navRow);

    workspaceTitleLabel = new QLabel(QStringLiteral("📁 本機磁碟 (Home)"), this);
    workspaceTitleLabel->setStyleSheet(QStringLiteral(
        "font-weight: bold; font-size: 14px; padding-bottom: 5px; color: palette(windowText);"));
    foldersLayout->addWidget(workspaceTitleLabel);

    folderTree = new QTreeView(this);
    folderTree->setMinimumWidth(250);
    folderTree->setHeaderHidden(true);
    folderTree->setAnimated(true);
    folderTree->setIndentation(18);
    folderTree->setExpandsOnDoubleClick(true);
    folderTree->setEditTriggers(QAbstractItemView::NoEditTriggers);

    folderModel = new QFileSystemModel(this);
    folderModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
    folderModel->setRootPath(QDir::homePath());
    proxyModel = new WorkspaceFilterProxyModel(this);
    proxyModel->setSourceModel(folderModel);
    proxyModel->setWorkspace(QDir::homePath());
    folderTree->setModel(proxyModel);
    for (int col = 1; col < folderModel->columnCount(); ++col) folderTree->hideColumn(col);

    foldersLayout->addWidget(folderTree);
    connect(folderTree, &QTreeView::clicked, this, [this](const QModelIndex &idx) {
        if (!idx.isValid()) return;
        const QModelIndex srcIdx = proxyModel ? proxyModel->mapToSource(idx) : idx;
        const QString selectedDir = folderModel->filePath(srcIdx);
        if (selectedDir.isEmpty()) return;
        QFileInfo fi(selectedDir);
        if (!fi.exists() || !fi.isDir()) return;
        fileListMode = FileListMode::PhysicalFolder;
        activeVirtualTag.clear();
        navigateToFolder(fi.absoluteFilePath(), true);
    });

    mainSplitter->addWidget(foldersPanel);

    // --- Column 3: Files (row1 sort, row2 filter+search) ---
    filesPanel = new QWidget(this);
    auto *filesLayout = new QVBoxLayout(filesPanel);
    lblFileListTitle = new QLabel(QStringLiteral("📂 檔案清單"), this);
    filesLayout->addWidget(lblFileListTitle);

    auto *controlsCol = new QVBoxLayout();

    auto *rowSort = new QHBoxLayout();
    cmbSort = new QComboBox(this);
    cmbSort->addItem(QStringLiteral("依名稱"));
    cmbSort->addItem(QStringLiteral("依日期"));
    cmbSort->addItem(QStringLiteral("依大小"));
    connect(cmbSort, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onSortChanged);
    rowSort->addWidget(cmbSort);
    rowSort->addStretch(1);
    controlsCol->addLayout(rowSort);

    auto *rowFilter = new QHBoxLayout();
    cmbTagFilter = new QComboBox(this);
    cmbTagFilter->addItem(LanguageManager::instance().getText(QStringLiteral("All Files")), QStringLiteral("ALL"));
    cmbTagFilter->setToolTip(QStringLiteral("🏷️ 標籤篩選"));
    rowFilter->addWidget(cmbTagFilter, 1);

    txtSearch = new QLineEdit(this);
    txtSearch->setPlaceholderText(QStringLiteral("🔍 搜尋"));
    rowFilter->addWidget(txtSearch, 2);
    controlsCol->addLayout(rowFilter);

    filesLayout->addLayout(controlsCol);

    fileList = new QListWidget(this);
    fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    fileList->setItemDelegate(new FileItemDelegate(fileList));
    connect(fileList, &QListWidget::itemClicked, this, &MainWindow::onFileSelected);
    connect(fileList, &QListWidget::customContextMenuRequested, this, &MainWindow::showFileContextMenu);
    connect(fileList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item) return;
        const QString absPath = item->data(Qt::UserRole).toString();
        if (absPath.isEmpty()) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(absPath));
    });
    filesLayout->addWidget(fileList, 1);

    auto *loadRow = new QHBoxLayout();
    btnLoadMore = new QPushButton(QStringLiteral("載入更多 (%1)").arg(BATCH_SIZE), this);
    btnLoadAll = new QPushButton(QStringLiteral("載入全部"), this);
    loadRow->addWidget(btnLoadMore);
    loadRow->addWidget(btnLoadAll);
    loadRow->addStretch(1);
    filesLayout->addLayout(loadRow);

    btnLoadMore->hide();
    btnLoadAll->hide();
    connect(btnLoadMore, &QPushButton::clicked, this, [this]() { renderFileListBatch(BATCH_SIZE); });
    connect(btnLoadAll, &QPushButton::clicked, this, [this]() {
        const int remaining = static_cast<int>(m_pendingFilesToDisplay.size()) - m_currentLoadedCount;
        renderFileListBatch(remaining);
    });

    connect(txtSearch, &QLineEdit::textChanged, this, &MainWindow::filterFiles);
    connect(cmbTagFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        syncTagListFromTagFilter();
        filterFiles();
    });

    mainSplitter->addWidget(filesPanel);

    // --- Column 4: Preview ---
    previewPanel = new QWidget(this);
    auto *previewLayout = new QVBoxLayout(previewPanel);
    lblPreviewTitle = new QLabel(QStringLiteral("👁️ 預覽與控制"), this);
    previewLayout->addWidget(lblPreviewTitle);

    lblPreviewImage = new QLabel(QStringLiteral("選擇檔案以預覽"), this);
    lblPreviewImage->setAlignment(Qt::AlignCenter);
    lblPreviewImage->setStyleSheet(QStringLiteral("border: 1px dashed gray; min-height: 200px;"));
    previewLayout->addWidget(lblPreviewImage);

    txtPreviewText = new QTextEdit(this);
    txtPreviewText->setReadOnly(true);
    txtPreviewText->setVisible(false);
    previewLayout->addWidget(txtPreviewText);

    lblTags = new QLabel(QStringLiteral("標籤: --"), this);
    lblTags->setWordWrap(true);
    lblTags->setStyleSheet(QStringLiteral("font-weight: bold; margin-top: 8px;"));
    previewLayout->addWidget(lblTags);

    lblStatus = new QLabel(QStringLiteral("狀態: 就緒"), this);
    lblStatus->setWordWrap(true);
    previewLayout->addWidget(lblStatus);

    // ===== Group 1: Tag management =====
    grpTagManagement = new QGroupBox(QStringLiteral("標籤管理"), this);
    auto *tagGroupLayout = new QVBoxLayout(grpTagManagement);

    auto *tagRow1 = new QHBoxLayout();
    btnSaveTags = new QPushButton(QStringLiteral("💾 儲存"), this);
    connect(btnSaveTags, &QPushButton::clicked, this, &MainWindow::saveTags);
    btnSaveTags->setEnabled(false);
    tagRow1->addWidget(btnSaveTags);
    tagRow1->addStretch(1);
    tagGroupLayout->addLayout(tagRow1);

    auto *tagRow2 = new QHBoxLayout();
    btnAddTag = new QPushButton(QStringLiteral("➕ 加入標籤"), this);
    connect(btnAddTag, &QPushButton::clicked, this, &MainWindow::addTag);
    tagRow2->addWidget(btnAddTag);

    btnRemoveTag = new QPushButton(QStringLiteral("➖ 移除標籤"), this);
    connect(btnRemoveTag, &QPushButton::clicked, this, &MainWindow::removeTag);
    tagRow2->addWidget(btnRemoveTag);
    tagRow2->addStretch(1);
    tagGroupLayout->addLayout(tagRow2);

    btnAddExistingTag = new QPushButton(QStringLiteral("🏷️ 加入現有標籤"), this);
    tagGroupLayout->addWidget(btnAddExistingTag);
    rebuildAddExistingTagMenu();

    previewLayout->addWidget(grpTagManagement);

    // ===== Group 2: File operations =====
    grpFileOperations = new QGroupBox(QStringLiteral("檔案操作"), this);
    auto *fileGroupLayout = new QVBoxLayout(grpFileOperations);

    auto *analysisRow = new QHBoxLayout();
    btnAnalyzeFile = new QPushButton(QStringLiteral("✨ 分析"), this);
    connect(btnAnalyzeFile, &QPushButton::clicked, this, &MainWindow::analyzeFile);
    analysisRow->addWidget(btnAnalyzeFile);

    btnCancelAnalysis = new QPushButton(QStringLiteral("⛔ 取消"), this);
    connect(btnCancelAnalysis, &QPushButton::clicked, this, &MainWindow::cancelAnalysis);
    btnCancelAnalysis->setEnabled(false);
    analysisRow->addWidget(btnCancelAnalysis);
    analysisRow->addStretch(1);
    fileGroupLayout->addLayout(analysisRow);

    auto *fileOpsRow = new QHBoxLayout();
    btnRenameFile = new QPushButton(QStringLiteral("重新命名"), this);
    btnDeleteFile = new QPushButton(QStringLiteral("刪除檔案"), this);
    btnRevealFile = new QPushButton(QStringLiteral("開啟位置"), this);
    connect(btnRenameFile, &QPushButton::clicked, this, &MainWindow::renameCurrentFile);
    connect(btnDeleteFile, &QPushButton::clicked, this, &MainWindow::deleteCurrentFile);
    connect(btnRevealFile, &QPushButton::clicked, this, &MainWindow::revealCurrentFile);
    fileOpsRow->addWidget(btnRenameFile);
    fileOpsRow->addWidget(btnDeleteFile);
    fileOpsRow->addWidget(btnRevealFile);
    fileOpsRow->addStretch(1);
    fileGroupLayout->addLayout(fileOpsRow);

    auto *archiveRow = new QHBoxLayout();
    btnPhysicalArchive = new QPushButton(QStringLiteral("實體歸檔 (依標籤)"), this);
    connect(btnPhysicalArchive, &QPushButton::clicked, this, &MainWindow::physicalArchiveFiles);
    archiveRow->addWidget(btnPhysicalArchive);

    btnUndoPhysicalArchive = new QPushButton(QStringLiteral("回上一步 (復原歸檔)"), this);
    connect(btnUndoPhysicalArchive, &QPushButton::clicked, this, &MainWindow::undoLastPhysicalArchive);
    btnUndoPhysicalArchive->setEnabled(false);
    archiveRow->addWidget(btnUndoPhysicalArchive);
    archiveRow->addStretch(1);
    fileGroupLayout->addLayout(archiveRow);

    previewLayout->addWidget(grpFileOperations);

    previewLayout->addStretch(1);
    mainSplitter->addWidget(previewPanel);

    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 2);
    mainSplitter->setStretchFactor(2, 4);
    mainSplitter->setStretchFactor(3, 3);

    syncNavigationButtons();
}

void MainWindow::setupContextMenus() {
    connect(tagListWidget, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *it = tagListWidget->itemAt(pos);
        if (!it) return;
        if (it->data(Qt::UserRole).toString() == QStringLiteral("ALL")) return;

        const QString rawTag = it->data(Qt::UserRole).toString();
        if (rawTag.isEmpty()) return;

        QMenu menu(this);
        QAction *actRename = menu.addAction(QStringLiteral("重新命名"));
        QAction *actDelete = menu.addAction(QStringLiteral("刪除（全域）"));
        QAction *chosen = menu.exec(tagListWidget->mapToGlobal(pos));
        if (!chosen) return;

        if (chosen == actRename) {
            bool ok = false;
            const QString newTag = QInputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("New name:"), QLineEdit::Normal, rawTag, &ok).trimmed();
            if (!ok || newTag.isEmpty() || newTag == rawTag) return;
            QMutexLocker locker(&tagMutex);
            tagManager.renameTag(rawTag, newTag);
            tagManager.saveTags();
        } else if (chosen == actDelete) {
            QMutexLocker locker(&tagMutex);
            tagManager.deleteTag(rawTag);
            tagManager.saveTags();
        }
        fileListMode = FileListMode::PhysicalFolder;
        activeVirtualTag.clear();
        updateTagList();
        scanFiles();
    });

}

void MainWindow::showFileContextMenu(const QPoint &pos) {
    // 1. 確認事件迴圈是否成功捕捉到訊號
    qDebug() << "[Debug] 觸發右鍵選單，接收到 Viewport 座標：" << pos;

    if (!fileList) {
        qDebug() << "[Error] fileList 元件不存在或未初始化。";
        return;
    }

    // 2. 執行 Hit-Testing
    QListWidgetItem *item = fileList->itemAt(pos);
    if (!item) {
        // 如果點到空白處，給予明確提示，而不是安靜地 return
        qDebug() << "[Debug] 點擊到空白區域，沒有選中任何具體檔案項目。";
        return;
    }

    // 3. 【關鍵修正】強制將右鍵點擊的項目設為「當前選取」狀態
    fileList->setCurrentItem(item);

    const QString filePath = item->data(Qt::UserRole).toString();
    if (filePath.isEmpty()) {
        qDebug() << "[Error] 選中項目未綁定有效的文件路徑資料。";
        return;
    }

    qDebug() << "[Debug] 成功鎖定檔案，準備彈出選單：" << filePath;

    // 4. 建立並配置右鍵選單
    QMenu menu(this);
    QAction *actRename = menu.addAction(QStringLiteral("重新命名"));
    QAction *actDelete = menu.addAction(QStringLiteral("刪除"));
    menu.addSeparator();
    QAction *actReveal = menu.addAction(QStringLiteral("在資料夾中顯示"));

    // 5. 將 Viewport 座標轉換為全域螢幕座標並阻塞執行
    QAction *chosen = menu.exec(fileList->viewport()->mapToGlobal(pos));
    if (!chosen) {
        qDebug() << "[Debug] 使用者取消了選單。";
        return;
    }

    // 6. 處理對應的 Action 邏輯
    if (chosen == actRename) {
        const QFileInfo oldInfo(filePath);
        const QString oldName = oldInfo.fileName();
        bool ok = false;

        const QString newName = QInputDialog::getText(
                                    this,
                                    QStringLiteral("重新命名"),
                                    QStringLiteral("新的檔名："),
                                    QLineEdit::Normal,
                                    oldName,
                                    &ok)
                                    .trimmed();

        if (!ok || newName.isEmpty() || newName == oldName) return;

        const QString newPath = oldInfo.dir().filePath(newName);

        if (QFileInfo::exists(newPath)) {
            QMessageBox::warning(this, QStringLiteral("重新命名失敗"), QStringLiteral("目標檔名已存在。"));
            return;
        }

        if (!QFile::rename(filePath, newPath)) {
            QMessageBox::warning(this, QStringLiteral("重新命名失敗"), QStringLiteral("檔案可能被占用或沒有權限。"));
            return;
        }

        const QFileInfo newInfo(newPath);
        item->setText(newInfo.fileName());
        item->setData(Qt::UserRole, newPath);

        if (fileListMode == FileListMode::VirtualTag) {
            QString relativePath = QDir(rootPath).relativeFilePath(newInfo.absolutePath());
            if (relativePath == QStringLiteral(".")) relativePath = QStringLiteral("根目錄");
            item->setData(Qt::UserRole + 1, relativePath);
        }
        onFileSelected(item);
        qDebug() << "[Debug] 重新命名成功：" << newName;
        return;
    }

    if (chosen == actDelete) {
        const int ret = QMessageBox::question(
            this,
            QStringLiteral("刪除確認"),
            QStringLiteral("確定要刪除「%1」嗎？").arg(QFileInfo(filePath).fileName()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);

        if (ret != QMessageBox::Yes) return;

        QFile file(filePath);
        bool removed = file.moveToTrash();
        if (!removed) removed = file.remove();

        if (!removed) {
            QMessageBox::warning(this, QStringLiteral("刪除失敗"), QStringLiteral("檔案可能被鎖定或沒有權限。"));
            return;
        }

        delete fileList->takeItem(fileList->row(item));
        qDebug() << "[Debug] 檔案刪除成功：" << filePath;
        return;
    }

    if (chosen == actReveal) {
        const QFileInfo fi(filePath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
        qDebug() << "[Debug] 開啟檔案位置：" << fi.absolutePath();
    }
}

void MainWindow::renameCurrentFile() {
    const QString filePath = currentFilePath();
    if (filePath.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("重新命名"), QStringLiteral("請先選取要重新命名的檔案。"));
        return;
    }

    const QFileInfo oldInfo(filePath);
    const QString oldName = oldInfo.fileName();
    const QString oldBaseName = oldInfo.completeBaseName();
    const QString oldSuffix = oldInfo.suffix();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("重新命名"));

    auto *root = new QVBoxLayout(&dialog);

    auto *inputRow = new QHBoxLayout();
    auto *nameEdit = new QLineEdit(&dialog);
    auto *suffixLabel = new QLabel(&dialog);

    nameEdit->setText(oldBaseName);
    suffixLabel->setText(oldSuffix.isEmpty() ? QString() : QStringLiteral(".%1").arg(oldSuffix));
    suffixLabel->setVisible(!oldSuffix.isEmpty());

    inputRow->addWidget(nameEdit, 1);
    inputRow->addWidget(suffixLabel);
    root->addLayout(inputRow);

    auto *chkEditSuffix = new QCheckBox(QStringLiteral("修改副檔名"), &dialog);
    chkEditSuffix->setChecked(false);
    root->addWidget(chkEditSuffix);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QObject::connect(chkEditSuffix, &QCheckBox::toggled, &dialog, [=](bool checked) {
        if (checked) {
            nameEdit->setText(oldName);
            suffixLabel->setVisible(false);
        } else {
            const QString current = nameEdit->text().trimmed();
            QString base = current;
            if (!oldSuffix.isEmpty()) {
                const QString dotExt = QStringLiteral(".%1").arg(oldSuffix);
                if (base.endsWith(dotExt, Qt::CaseInsensitive)) {
                    base.chop(dotExt.size());
                } else {
                    const int lastDot = base.lastIndexOf('.');
                    if (lastDot > 0) base = base.left(lastDot);
                }
            } else {
                const int lastDot = base.lastIndexOf('.');
                if (lastDot > 0) base = base.left(lastDot);
            }

            nameEdit->setText(base);
            suffixLabel->setText(oldSuffix.isEmpty() ? QString() : QStringLiteral(".%1").arg(oldSuffix));
            suffixLabel->setVisible(!oldSuffix.isEmpty());
        }
    });

    nameEdit->selectAll();
    nameEdit->setFocus();

    if (dialog.exec() != QDialog::Accepted) return;

    const bool editSuffix = chkEditSuffix->isChecked();
    const QString userText = nameEdit->text().trimmed();
    if (userText.isEmpty()) return;

    QString finalName;
    if (!editSuffix && !oldSuffix.isEmpty()) {
        finalName = userText + QStringLiteral(".") + oldSuffix;
    } else {
        finalName = userText;
    }

    if (finalName.isEmpty() || finalName == oldName) return;

    const QString newPath = oldInfo.dir().filePath(finalName);
    if (QFileInfo::exists(newPath)) {
        QMessageBox::warning(this, QStringLiteral("重新命名失敗"), QStringLiteral("目標檔名已存在。"));
        return;
    }

    if (!QFile::rename(filePath, newPath)) {
        QMessageBox::warning(this, QStringLiteral("重新命名失敗"), QStringLiteral("檔案可能被占用或沒有權限。"));
        return;
    }

    // 更新目前選取項目的顯示與資料綁定
    if (auto *item = fileList ? fileList->currentItem() : nullptr) {
        const QFileInfo newInfo(newPath);
        item->setText(newInfo.fileName());
        item->setData(Qt::UserRole, newPath);
        if (fileListMode == FileListMode::VirtualTag) {
            QString relativePath = QDir(rootPath).relativeFilePath(newInfo.absolutePath());
            if (relativePath == QStringLiteral(".")) relativePath = QStringLiteral("根目錄");
            item->setData(Qt::UserRole + 1, relativePath);
        }
        onFileSelected(item);
    } else {
        scanFiles();
        sortFileList();
    }
}

void MainWindow::deleteCurrentFile() {
    const QString filePath = currentFilePath();
    if (filePath.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("刪除"), QStringLiteral("請先選取要刪除的檔案。"));
        return;
    }

    const int ret = QMessageBox::question(
        this,
        QStringLiteral("刪除確認"),
        QStringLiteral("確定要刪除「%1」嗎？").arg(QFileInfo(filePath).fileName()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    QFile file(filePath);
    bool removed = file.moveToTrash();
    if (!removed) removed = file.remove();
    if (!removed) {
        QMessageBox::warning(this, QStringLiteral("刪除失敗"), QStringLiteral("檔案可能被鎖定或沒有權限。"));
        return;
    }

    if (fileList) {
        if (auto *item = fileList->currentItem()) {
            delete fileList->takeItem(fileList->row(item));
        }
    }
}

void MainWindow::revealCurrentFile() {
    const QString filePath = currentFilePath();
    if (filePath.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("開啟位置"), QStringLiteral("請先選取檔案。"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
}

namespace {
QString sanitizeTagFolderName(const QString &tag) {
    QString s = tag.trimmed();
    if (s.isEmpty()) {
        return QStringLiteral("_未命名標籤");
    }
    const QString invalid = QStringLiteral("<>:\"/\\|?*\r\n");
    for (QChar c : invalid) {
        s.replace(c, QLatin1Char('_'));
    }
    if (s == QLatin1String(".") || s == QLatin1String("..")) {
        s = QLatin1Char('_') + s;
    }
    return s;
}
} // namespace

void MainWindow::physicalArchiveFiles() {
    if (rootPath.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("實體歸檔 (依標籤)"),
                                 QStringLiteral("請先開啟工作區資料夾。"));
        return;
    }

    const QString rootClean = QDir::cleanPath(rootPath);
    const QString homeClean = QDir::cleanPath(QDir::homePath());
    const QString desktopClean = QDir::cleanPath(QDir(homeClean).filePath(QStringLiteral("Desktop")));

    const bool isRootDir = QDir(rootClean).isRoot()
#ifdef Q_OS_WIN
                           || QRegularExpression(QStringLiteral("^[A-Za-z]:/$")).match(rootClean + QLatin1Char('/')).hasMatch()
#endif
        ;
    const bool isHighRisk = isRootDir || rootClean == homeClean || rootClean == desktopClean;
    if (isHighRisk) {
        QMessageBox::critical(
            this,
            QStringLiteral("實體歸檔 (依標籤)"),
            QStringLiteral("為保護系統安全，禁止對系統核心或使用者根目錄執行全域實體歸檔！請指定特定工作資料夾。"));
        return;
    }

    const int answer = QMessageBox::question(
        this,
        QStringLiteral("實體歸檔 (依標籤)"),
        QStringLiteral("將對以下目錄進行實體歸檔：\n【%1】\n此操作將改變檔案實體位置，是否繼續？").arg(rootPath),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    m_lastMoveHistory.clear();
    if (btnUndoPhysicalArchive) {
        btnUndoPhysicalArchive->setEnabled(false);
    }

    std::vector<std::pair<QString, QString>> pairs;
    {
        QMutexLocker locker(&tagMutex);
        pairs = tagManager.taggedFilesWithPrimaryTag();
    }

    for (const auto &entry : pairs) {
        const QString srcPath = QDir::cleanPath(entry.first);
        const QString &rawTag = entry.second;

        const QFileInfo fiSrc(srcPath);
        if (!fiSrc.exists() || !fiSrc.isFile()) {
            qDebug() << "physicalArchiveFiles: skip (missing or not a file):" << srcPath;
            continue;
        }

        const QString rel = QDir(rootClean).relativeFilePath(fiSrc.absoluteFilePath());
        if (rel.startsWith(QStringLiteral(".."))) {
            qDebug() << "physicalArchiveFiles: skip (outside root):" << srcPath;
            continue;
        }
        if (rel == QStringLiteral(".smartfile") || rel.startsWith(QStringLiteral(".smartfile/"))) {
            qDebug() << "physicalArchiveFiles: skip (.smartfile):" << srcPath;
            continue;
        }

        const QString folderName = sanitizeTagFolderName(rawTag);
        const QString destDir = QDir(rootClean).absoluteFilePath(folderName);
        const QString destPath = QDir(destDir).absoluteFilePath(fiSrc.fileName());

        if (QDir::cleanPath(fiSrc.absolutePath()) == QDir::cleanPath(destDir)) {
            continue;
        }

        if (QFile::exists(destPath)) {
            qDebug() << "physicalArchiveFiles: skip (target exists):" << destPath;
            continue;
        }

        if (!QDir().mkpath(destDir)) {
            qDebug() << "physicalArchiveFiles: mkpath failed:" << destDir;
            continue;
        }

        QFile f(srcPath);
        if (!f.rename(destPath)) {
            qDebug() << "physicalArchiveFiles: rename failed" << srcPath << "->" << destPath << f.errorString();
            continue;
        }

        m_lastMoveHistory.push_back(qMakePair(destPath, srcPath));

        {
            QMutexLocker locker(&tagMutex);
            tagManager.relocateFilePath(srcPath, destPath, false);
        }
    }

    {
        QMutexLocker locker(&tagMutex);
        tagManager.saveTags();
    }

    if (btnUndoPhysicalArchive) {
        btnUndoPhysicalArchive->setEnabled(!m_lastMoveHistory.isEmpty());
    }

    scanFiles();
    updateTagList();
    const QString fp = currentFilePath();
    if (!fp.isEmpty()) {
        updateTagDisplayForFile(fp);
    }
}

void MainWindow::undoLastPhysicalArchive() {
    if (m_lastMoveHistory.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("回上一步 (復原歸檔)"), QStringLiteral("沒有可復原的歸檔紀錄。"));
        return;
    }

    const int answer = QMessageBox::question(
        this,
        QStringLiteral("回上一步 (復原歸檔)"),
        QStringLiteral("將會把上一輪實體歸檔移動的檔案全部搬回原路徑。是否繼續？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    bool movedAny = false;
    for (const auto &p : m_lastMoveHistory) {
        const QString newPath = QDir::cleanPath(p.first);
        const QString oldPath = QDir::cleanPath(p.second);

        const QFileInfo fiNew(newPath);
        if (!fiNew.exists() || !fiNew.isFile()) {
            qDebug() << "undoLastPhysicalArchive: skip (missing):" << newPath;
            continue;
        }

        if (QFile::exists(oldPath)) {
            qDebug() << "undoLastPhysicalArchive: skip (old path exists):" << oldPath;
            continue;
        }

        const QString oldDir = QFileInfo(oldPath).absolutePath();
        if (!QDir().mkpath(oldDir)) {
            qDebug() << "undoLastPhysicalArchive: mkpath failed:" << oldDir;
            continue;
        }

        QFile f(newPath);
        if (!f.rename(oldPath)) {
            qDebug() << "undoLastPhysicalArchive: rename failed" << newPath << "->" << oldPath << f.errorString();
            continue;
        }

        movedAny = true;
        {
            QMutexLocker locker(&tagMutex);
            tagManager.relocateFilePath(newPath, oldPath, false);
        }
    }

    if (movedAny) {
        QMutexLocker locker(&tagMutex);
        tagManager.saveTags();
    }

    m_lastMoveHistory.clear();
    if (btnUndoPhysicalArchive) {
        btnUndoPhysicalArchive->setEnabled(false);
    }

    scanFiles();
    updateTagList();
    const QString fp = currentFilePath();
    if (!fp.isEmpty()) {
        updateTagDisplayForFile(fp);
    }
}

void MainWindow::mapsHomeFixAndSetRoot(const QString &dir) {
    QFileInfo fi(dir);
    const QString abs = fi.exists() ? fi.absoluteFilePath() : QDir::homePath();
    rootPath = abs;
    currentPath = abs;

    if (m_dirWatcher) {
        const QStringList oldDirs = m_dirWatcher->directories();
        if (!oldDirs.isEmpty()) m_dirWatcher->removePaths(oldDirs);
        if (!rootPath.isEmpty()) {
            if (!m_dirWatcher->addPath(rootPath)) {
                qDebug() << "QFileSystemWatcher addPath failed:" << rootPath;
            }
        }
    }

    folderModel->setRootPath(rootPath);
    if (proxyModel) {
        proxyModel->setWorkspace(rootPath);
        const QString parentDir = QFileInfo(rootPath).path();
        folderTree->setRootIndex(proxyModel->mapFromSource(folderModel->index(parentDir)));
    }
    setFolderTreeCurrentPath(rootPath);

    tagManager.loadTags(rootPath.toStdString());
}

void MainWindow::setFolderTreeCurrentPath(const QString &absDir) {
    const QModelIndex srcIdx = folderModel->index(absDir);
    const QModelIndex idx = proxyModel ? proxyModel->mapFromSource(srcIdx) : srcIdx;
    if (idx.isValid()) {
        folderTree->setCurrentIndex(idx);
        folderTree->scrollTo(idx, QAbstractItemView::PositionAtCenter);
        folderTree->expand(idx);
    }
}

void MainWindow::pushHistory(const QString &path) {
    if (path.isEmpty()) return;
    if (navIndex >= 0 && navIndex < navHistory.size() && navHistory[navIndex] == path) {
        syncNavigationButtons();
        return;
    }
    while (navHistory.size() > navIndex + 1) navHistory.removeLast();
    navHistory.push_back(path);
    navIndex = navHistory.size() - 1;
    syncNavigationButtons();
}

void MainWindow::syncNavigationButtons() {
    btnBack->setEnabled(navIndex > 0);
    btnForward->setEnabled(navIndex >= 0 && navIndex + 1 < navHistory.size());
}

void MainWindow::navigateToFolder(const QString &path, bool pushToHistory) {
    if (path.isEmpty()) return;
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isDir()) return;
    currentPath = fi.absoluteFilePath();
    if (pushToHistory) pushHistory(currentPath);
    setFolderTreeCurrentPath(currentPath);
    scanFiles();
    sortFileList();
}

void MainWindow::goBack() {
    if (navIndex <= 0) return;
    --navIndex;
    const QString path = navHistory[navIndex];
    currentPath = path;
    fileListMode = FileListMode::PhysicalFolder;
    activeVirtualTag.clear();
    setFolderTreeCurrentPath(currentPath);
    syncNavigationButtons();
    scanFiles();
    sortFileList();
}

void MainWindow::goForward() {
    if (navIndex + 1 >= navHistory.size()) return;
    ++navIndex;
    const QString path = navHistory[navIndex];
    currentPath = path;
    fileListMode = FileListMode::PhysicalFolder;
    activeVirtualTag.clear();
    setFolderTreeCurrentPath(currentPath);
    syncNavigationButtons();
    scanFiles();
    sortFileList();
}

void MainWindow::goHome() {
    const QString home = QDir::homePath();
    rootPath = home;
    currentPath = home;

    if (m_dirWatcher) {
        const QStringList oldDirs = m_dirWatcher->directories();
        if (!oldDirs.isEmpty()) m_dirWatcher->removePaths(oldDirs);
        if (!rootPath.isEmpty()) {
            if (!m_dirWatcher->addPath(rootPath)) {
                qDebug() << "QFileSystemWatcher addPath failed:" << rootPath;
            }
        }
    }

    folderModel->setRootPath(rootPath);
    if (workspaceTitleLabel) workspaceTitleLabel->setText(QStringLiteral("📁 %1").arg(LanguageManager::instance().getText(QStringLiteral("本機磁碟 (Home)"))));
    if (proxyModel) {
        const QString homeParent = QFileInfo(home).path();
        proxyModel->setWorkspace(home);
        folderTree->setRootIndex(proxyModel->mapFromSource(folderModel->index(homeParent)));
    }
    fileListMode = FileListMode::PhysicalFolder;
    activeVirtualTag.clear();
    navHistory.clear();
    navIndex = -1;
    pushHistory(currentPath);
    setFolderTreeCurrentPath(currentPath);
    scanFiles();
    sortFileList();
}

void MainWindow::onSortChanged(int) {
    sortFileList();
}

void MainWindow::sortFileList() {
    if (!fileList) return;
    const int mode = cmbSort ? cmbSort->currentIndex() : 0;
    if (mode == 0) {
        QList<QListWidgetItem *> items;
        for (int i = 0; i < fileList->count(); ++i) items.push_back(fileList->takeItem(0));
        std::sort(items.begin(), items.end(), [](QListWidgetItem *a, QListWidgetItem *b) {
            const QString pa = a->data(Qt::UserRole).toString();
            const QString pb = b->data(Qt::UserRole).toString();
            return baseName(pa).localeAwareCompare(baseName(pb)) < 0;
        });
        for (auto *it : items) fileList->addItem(it);
        return;
    }

    QList<QListWidgetItem *> items;
    for (int i = 0; i < fileList->count(); ++i) items.push_back(fileList->takeItem(0));

    std::sort(items.begin(), items.end(), [mode](QListWidgetItem *a, QListWidgetItem *b) {
        const QString pa = a->data(Qt::UserRole).toString();
        const QString pb = b->data(Qt::UserRole).toString();
        const QFileInfo fa(pa);
        const QFileInfo fb(pb);
        if (mode == 1) {
            const auto ta = fa.lastModified();
            const auto tb = fb.lastModified();
            if (ta == tb) return baseName(pa).localeAwareCompare(baseName(pb)) < 0;
            return ta > tb;
        }
        const qint64 sa = fa.size();
        const qint64 sb = fb.size();
        if (sa == sb) return baseName(pa).localeAwareCompare(baseName(pb)) < 0;
        return sa > sb;
    });

    for (auto *it : items) fileList->addItem(it);
}

void MainWindow::openFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("選擇資料夾"),
        rootPath.isEmpty() ? QDir::homePath() : rootPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;
    mapsHomeFixAndSetRoot(dir);
    const QString folderName = QFileInfo(dir).fileName().isEmpty() ? dir : QFileInfo(dir).fileName();
    if (workspaceTitleLabel) workspaceTitleLabel->setText(QStringLiteral("📁 %1").arg(folderName));
    if (proxyModel) {
        proxyModel->setWorkspace(dir);
        const QString parentDir = QFileInfo(dir).path();
        folderTree->setRootIndex(proxyModel->mapFromSource(folderModel->index(parentDir)));
    }
    navHistory.clear();
    navIndex = -1;
    pushHistory(currentPath);
    fileListMode = FileListMode::PhysicalFolder;
    activeVirtualTag.clear();
    scanFiles();
    sortFileList();
}

QString MainWindow::currentFilePath() const {
    const auto selected = fileList->selectedItems();
    if (selected.isEmpty()) return {};
    return selected.first()->data(Qt::UserRole).toString();
}

bool MainWindow::isAnalyzableFile(const QFileInfo &fi) const {
    if (!fi.exists()) return false;
    if (fi.isDir()) return false;
    if (fi.isSymLink()) {
        const QString target = fi.symLinkTarget();
        if (!target.isEmpty() && QFileInfo(target).isDir()) return false;
    }
    return true;
}

void MainWindow::scanFiles() {
    if (fileListMode == FileListMode::VirtualTag) {
        populateVirtualTagFiles(activeVirtualTag);
    } else {
        scanPhysicalFolder();
    }

    if (m_mainTabWidget && m_graphWidget && m_graphTab && m_mainTabWidget->currentWidget() == m_graphTab) {
        m_graphWidget->buildGraph();
    }
}

void MainWindow::scanPhysicalFolder() {
    fileList->clear();
    m_pendingFilesToDisplay.clear();
    m_currentLoadedCount = 0;

    const bool recursive = chkRecursive && chkRecursive->isChecked();
    int count = 0;

    const QDirIterator::IteratorFlags flags =
        recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;

    QDirIterator it(currentPath, QDir::Files | QDir::NoDotAndDotDot, flags);
    while (it.hasNext()) {
        const QString filePath = it.next();
        const QFileInfo fileInfo(filePath);
        if (!fileInfo.exists()) continue;
        if (fileInfo.isDir()) continue;
        if (fileInfo.isSymLink()) {
            const QString target = fileInfo.symLinkTarget();
            if (!target.isEmpty() && QFileInfo(target).isDir()) continue;
        }
        const QString fileName = fileInfo.fileName();
        m_pendingFilesToDisplay.push_back(filePath);

        const QStringList fastTags = getFastPathTags(fileName);
        if (!fastTags.isEmpty()) {
            QMutexLocker locker(&tagMutex);
            const auto existingByPath = tagManager.getTags(filePath);
            const auto existingByName = tagManager.getTags(fileName);
            QSet<QString> existingSet;
            for (const auto &t : existingByPath) existingSet.insert(t);
            for (const auto &t : existingByName) existingSet.insert(t);
            for (const QString &t : fastTags) {
                if (existingSet.contains(t)) continue;
                tagManager.addTag(filePath, t, false);
                existingSet.insert(t);
            }
        }
        ++count;
    }

    {
        QMutexLocker locker(&tagMutex);
        tagManager.saveTags();
    }
    updateTagList();

    {
        auto &lm = LanguageManager::instance();
        const QString scope = recursive ? lm.getText(QStringLiteral("遞迴")) : lm.getText(QStringLiteral("僅此層"));
        lblStatus->setText(QStringLiteral("%1: %2 | %3: %4 [%5]")
                               .arg(lm.getText(QStringLiteral("資料夾")))
                               .arg(currentPath)
                               .arg(lm.getText(QStringLiteral("檔案數")))
                               .arg(count)
                               .arg(scope));
    }

    fileList->clear();
    renderFileListBatch(BATCH_SIZE);
}

void MainWindow::populateVirtualTagFiles(const QString &tag) {
    fileList->clear();
    m_pendingFilesToDisplay.clear();
    m_currentLoadedCount = 0;
    if (tag.isEmpty()) {
        if (btnLoadMore) btnLoadMore->hide();
        if (btnLoadAll) btnLoadAll->hide();
        return;
    }

    {
        QMutexLocker locker(&tagMutex);
        m_pendingFilesToDisplay = tagManager.getFilesByTag(tag);
    }

    auto translateVirtualTagForDisplay = [&](const QString &raw) {
        auto &lm = LanguageManager::instance();
        QString t = raw.trimmed();
        if (t.isEmpty()) return t;

        // Emoji prefixes can be multiple QChars (surrogates + variation selectors).
        QString prefix;
        int i = 0;
        while (i < t.size()) {
            const QChar c = t.at(i);
            if (c.isSpace()) break;
            if (c.isLetterOrNumber()) break;
            prefix.append(c);
            ++i;
        }
        const QString rest = t.mid(i).trimmed();
        if (!prefix.isEmpty() && !rest.isEmpty()) {
            const QString translatedRest = lm.getText(rest);
            if (translatedRest != rest) return QStringLiteral("%1 %2").arg(prefix, translatedRest).trimmed();
        }
        return lm.getText(t);
    };

    {
        auto &lm = LanguageManager::instance();
        lblStatus->setText(QStringLiteral("%1: %2 | %3: %4")
                               .arg(lm.getText(QStringLiteral("虛擬標籤檢視")))
                               .arg(translateVirtualTagForDisplay(tag))
                               .arg(lm.getText(QStringLiteral("檔案數")))
                               .arg(static_cast<int>(m_pendingFilesToDisplay.size())));
    }

    renderFileListBatch(BATCH_SIZE);
}

void MainWindow::renderFileListBatch(int count) {
    if (!fileList) return;
    if (count <= 0) {
        const bool hasMore = m_currentLoadedCount < static_cast<int>(m_pendingFilesToDisplay.size());
        if (btnLoadMore) btnLoadMore->setVisible(hasMore);
        if (btnLoadAll) btnLoadAll->setVisible(hasMore);
        return;
    }

    const int total = static_cast<int>(m_pendingFilesToDisplay.size());
    const int remaining = total - m_currentLoadedCount;
    const int take = std::min(count, remaining);
    if (take <= 0) {
        if (btnLoadMore) btnLoadMore->hide();
        if (btnLoadAll) btnLoadAll->hide();
        return;
    }

    const int start = m_currentLoadedCount;
    const int end = start + take;
    for (int i = start; i < end; ++i) {
        const QString filePath = m_pendingFilesToDisplay[static_cast<size_t>(i)];
        const QFileInfo fi(filePath);
        if (!fi.exists()) continue;
        if (fi.isDir()) continue;
        if (fi.isSymLink()) {
            const QString target = fi.symLinkTarget();
            if (!target.isEmpty() && QFileInfo(target).isDir()) continue;
        }

        if (fileListMode == FileListMode::VirtualTag) {
            QString parentPath = fi.absolutePath();
            QDir workspaceDir(rootPath);
            QString relativePath = workspaceDir.relativeFilePath(parentPath);
            if (relativePath == QStringLiteral(".")) {
                relativePath = QStringLiteral("根目錄");
            }

            auto *item = new QListWidgetItem();
            item->setText(fi.fileName());                     // 檔名（DisplayRole）
            item->setData(Qt::UserRole, filePath);            // 絕對路徑（雙擊用）
            item->setData(Qt::UserRole + 1, relativePath);    // 相對工作區路徑（Delegate 用）
            fileList->addItem(item);
        } else {
            const QString fileName = fi.fileName();
            auto *item = new QListWidgetItem(fileName, fileList);
            item->setData(Qt::UserRole, filePath);
        }
    }

    m_currentLoadedCount = end;

    const bool hasMore = m_currentLoadedCount < total;
    if (btnLoadMore) btnLoadMore->setVisible(hasMore);
    if (btnLoadAll) btnLoadAll->setVisible(hasMore);

    filterFiles();
    sortFileList();
}

void MainWindow::updateTagListCountsOnly() {
    for (int i = 0; i < tagListWidget->count(); ++i) {
        QListWidgetItem *it = tagListWidget->item(i);
        if (!it) continue;
        const QString role = it->data(Qt::UserRole).toString();
        if (role == QStringLiteral("ALL")) continue;
        const QString canon = normalizeDisplayTag(role);
        int n = 0;
        {
            QMutexLocker locker(&tagMutex);
            n = static_cast<int>(tagManager.getFilesByTag(canon).size());
        }
        const QString baseZh = systemTagBaseZh(canon);
        const QString emoji = systemTagEmojiPrefix(canon);
        const QString displayName = baseZh.isEmpty()
                                        ? canon
                                        : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
        it->setText(QStringLiteral("%1 (%2)").arg(displayName.trimmed()).arg(n));
        it->setData(Qt::UserRole, canon);          // keep internal tag for TagManager
        it->setData(Qt::UserRole + 1, n);          // count
        it->setData(Qt::UserRole + 2, baseZh);     // base zh for translation (if any)
    }
    syncTagFilterFromTagList();
}

void MainWindow::updateTagList() {
    tagListWidget->clear();

    auto *allItem = new QListWidgetItem(LanguageManager::instance().getText(QStringLiteral("All Files")), tagListWidget);
    allItem->setData(Qt::UserRole, QStringLiteral("ALL"));

    std::vector<QString> rawTags;
    {
        QMutexLocker locker(&tagMutex);
        rawTags = tagManager.getAllTags();
    }

    std::map<QString, QSet<QString>> normToFiles;
    for (const QString &t : rawTags) {
        const QString canon = normalizeDisplayTag(t);
        std::vector<QString> files;
        {
            QMutexLocker locker(&tagMutex);
            files = tagManager.getFilesByTag(t);
        }
        for (const QString &fp : files) normToFiles[canon].insert(fp);
    }

    const QStringList defaults = {kTagImage, kTagVideo, kTagDoc, kTagAudio};
    QSet<QString> keys;
    for (const auto &kv : normToFiles) keys.insert(kv.first);
    for (const QString &d : defaults) keys.insert(normalizeDisplayTag(d));

    QList<QString> ordered = keys.values();
    std::sort(ordered.begin(), ordered.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });

    for (const QString &canon : ordered) {
        int n = 0;
        if (normToFiles.count(canon)) n = static_cast<int>(normToFiles[canon].size());
        else {
            QMutexLocker locker(&tagMutex);
            n = static_cast<int>(tagManager.getFilesByTag(canon).size());
        }
        const QString baseZh = systemTagBaseZh(canon);
        const QString emoji = systemTagEmojiPrefix(canon);
        const QString displayName = baseZh.isEmpty()
                                        ? canon
                                        : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
        auto *it = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(displayName.trimmed()).arg(n), tagListWidget);
        it->setData(Qt::UserRole, canon);      // internal/original tag
        it->setData(Qt::UserRole + 1, n);      // count
        it->setData(Qt::UserRole + 2, baseZh); // base zh (optional)
    }

    syncTagFilterFromTagList();
    rebuildAddExistingTagMenu();
}

void MainWindow::syncTagFilterFromTagList() {
    const QString prevData = cmbTagFilter->currentData().toString();
    cmbTagFilter->blockSignals(true);
    cmbTagFilter->clear();
    cmbTagFilter->addItem(LanguageManager::instance().getText(QStringLiteral("All Files")), QStringLiteral("ALL"));
    for (int i = 0; i < tagListWidget->count(); ++i) {
        const auto *it = tagListWidget->item(i);
        if (!it) continue;
        if (it->data(Qt::UserRole).toString() == QStringLiteral("ALL")) continue;
        const QString rawTag = it->data(Qt::UserRole).toString();
        if (rawTag.isEmpty()) continue;
        const QString baseZh = it->data(Qt::UserRole + 2).toString();
        const QString canon = normalizeDisplayTag(rawTag);
        const QString emoji = systemTagEmojiPrefix(canon);
        const QString displayName = baseZh.isEmpty()
                                        ? canon
                                        : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
        cmbTagFilter->addItem(displayName.trimmed(), rawTag);
    }
    const int idx = cmbTagFilter->findData(prevData);
    cmbTagFilter->setCurrentIndex(idx >= 0 ? idx : 0);
    cmbTagFilter->blockSignals(false);
}

void MainWindow::syncTagListFromTagFilter() {
    const QString selected = cmbTagFilter->currentData().toString();
    if (selected == QStringLiteral("ALL") || selected.isEmpty()) {
        for (int i = 0; i < tagListWidget->count(); ++i) {
            auto *it = tagListWidget->item(i);
            if (it && it->data(Qt::UserRole).toString() == QStringLiteral("ALL")) {
                tagListWidget->setCurrentItem(it);
                break;
            }
        }
        return;
    }
    for (int i = 0; i < tagListWidget->count(); ++i) {
        auto *it = tagListWidget->item(i);
        if (it && it->data(Qt::UserRole).toString() == selected) {
            tagListWidget->setCurrentItem(it);
            break;
        }
    }
}

void MainWindow::filterFiles() {
    const QString query = txtSearch->text().trimmed().toLower();
    const QString tagFilter = cmbTagFilter->currentData().toString();

    std::vector<QString> filesWithTag;
    if (!tagFilter.isEmpty() && tagFilter != QStringLiteral("ALL")) {
        QMutexLocker locker(&tagMutex);
        filesWithTag = tagManager.getFilesByTag(tagFilter);
    }

    for (int i = 0; i < fileList->count(); ++i) {
        auto *it = fileList->item(i);
        if (!it) continue;

        const QString absPath = it->data(Qt::UserRole).toString();
        const QString nameLower = baseName(absPath).toLower();

        bool match = true;
        if (!query.isEmpty()) {
            match = nameLower.contains(query) || parentDirDisplay(absPath).toLower().contains(query);
            if (!match) {
                std::vector<QString> tags;
                {
                    QMutexLocker locker(&tagMutex);
                    tags = tagManager.getTags(absPath);
                }
                for (const auto &t : tags) {
                    if (t.toLower().contains(query)) {
                        match = true;
                        break;
                    }
                }
            }
        }

        if (match && !tagFilter.isEmpty() && tagFilter != QStringLiteral("ALL")) {
            bool tagOk = false;
            for (const auto &fp : filesWithTag) {
                if (fp == absPath || baseName(fp) == baseName(absPath)) {
                    tagOk = true;
                    break;
                }
            }
            match = tagOk;
        }

        it->setHidden(!match);
    }
}

void MainWindow::onFileSelected(QListWidgetItem *item) {
    if (!item) return;
    const QString absPath = item->data(Qt::UserRole).toString();
    if (absPath.isEmpty()) return;

    QFileInfo fi(absPath);
    btnAnalyzeFile->setEnabled(isAnalyzableFile(fi));

    updatePreviewForFile(absPath);
    updateTagDisplayForFile(absPath);
    btnSaveTags->setEnabled(false);
}

void MainWindow::onTagSelected(QListWidgetItem *item) {
    if (!item) return;
    const QString data = item->data(Qt::UserRole).toString();
    if (data == QStringLiteral("ALL")) {
        fileListMode = FileListMode::PhysicalFolder;
        activeVirtualTag.clear();
        cmbTagFilter->setCurrentIndex(0);
        scanFiles();
        sortFileList();
        return;
    }

    const QString tag = normalizeDisplayTag(data);
    fileListMode = FileListMode::VirtualTag;
    activeVirtualTag = tag;
    const int idx = cmbTagFilter->findText(tag);
    if (idx >= 0) cmbTagFilter->setCurrentIndex(idx);
    populateVirtualTagFiles(tag);
}

void MainWindow::updatePreviewForFile(const QString &absPath) {
    QFileInfo fi(absPath);
    QMimeDatabase db;
    const QMimeType mt = db.mimeTypeForFile(fi);
    const QString typeLine = QStringLiteral("[ %1 %2 ]").arg(emojiForMime(mt), mimeDisplay(mt));

    lblPreviewImage->setVisible(false);
    txtPreviewText->setVisible(false);

    if (!fi.exists()) {
        txtPreviewText->setVisible(true);
        txtPreviewText->setPlainText(typeLine + QStringLiteral("\n(檔案不存在)"));
        return;
    }

    if (mt.name().startsWith(QStringLiteral("image/"))) {
        QPixmap pix(absPath);
        if (!pix.isNull()) {
            lblPreviewImage->setVisible(true);
            lblPreviewImage->setPixmap(pix.scaled(lblPreviewImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            txtPreviewText->setVisible(true);
            txtPreviewText->setPlainText(typeLine + QStringLiteral("\n(無法載入圖片)"));
        }
        return;
    }

    const QString suffix = fi.suffix().toLower();
    if (suffix == QStringLiteral("pdf") || suffix == QStringLiteral("docx") || suffix == QStringLiteral("xlsx")) {
        txtPreviewText->setVisible(true);
        std::string content = DocumentParser::extractText(absPath.toStdString());
        if (content.size() > 2500) content = content.substr(0, 2500) + "...";
        if (content.empty()) content = "(No searchable text found or encrypted)";
        txtPreviewText->setPlainText(typeLine + QStringLiteral("\n") + QString::fromStdString(content));
        return;
    }

    if (mt.name().startsWith(QStringLiteral("text/")) || suffix == QStringLiteral("md") || suffix == QStringLiteral("txt") || suffix == QStringLiteral("log") || suffix == QStringLiteral("json") || suffix == QStringLiteral("xml") || suffix == QStringLiteral("yaml") || suffix == QStringLiteral("yml") || suffix == QStringLiteral("cpp") || suffix == QStringLiteral("h") || suffix == QStringLiteral("py") || suffix == QStringLiteral("js") || suffix == QStringLiteral("ts")) {
        txtPreviewText->setVisible(true);
        std::ifstream f(absPath.toStdString(), std::ios::binary);
        if (!f.is_open()) {
            txtPreviewText->setPlainText(typeLine + QStringLiteral("\n(無法讀取)"));
            return;
        }
        std::string buf;
        buf.resize(4096);
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        buf.resize(static_cast<size_t>(f.gcount()));
        txtPreviewText->setPlainText(typeLine + QStringLiteral("\n") + QString::fromUtf8(buf.data(), static_cast<int>(buf.size())));
        return;
    }

    txtPreviewText->setVisible(true);
    txtPreviewText->setPlainText(typeLine + QStringLiteral("\n(%1)")
                                     .arg(LanguageManager::instance().getText(QStringLiteral("二進位檔：不顯示內容"))));
}

void MainWindow::updateTagDisplayForFile(const QString &absPath) {
    std::vector<QString> tags;
    {
        QMutexLocker locker(&tagMutex);
        tags = tagManager.getTags(absPath);
    }

    auto isAiTag = [](const QString &t) {
        return t.trimmed().toLower().startsWith(QStringLiteral("[ai]"));
    };
    auto stripAiPrefix = [](const QString &t) {
        QString s = t.trimmed();
        const QString low = s.toLower();
        const QString pref = QStringLiteral("[ai]");
        if (low.startsWith(pref)) {
            s = s.mid(pref.size()).trimmed();
        }
        return s;
    };
    auto normBase = [&](const QString &t) {
        return stripAiPrefix(t).trimmed().toLower();
    };

    QSet<QString> manualBases;
    QStringList manualTags;
    QStringList aiTags;
    for (const auto &t : tags) {
        const QString base = normBase(t);
        if (base.isEmpty()) continue;
        if (isAiTag(t)) {
            aiTags << base;
        } else {
            manualBases.insert(base);
            manualTags << base;
        }
    }

    // Dedup: manual wins over AI.
    QStringList aiShown;
    for (const QString &b : aiTags) {
        if (manualBases.contains(b)) continue;
        aiShown << b;
    }

    manualTags.removeDuplicates();
    aiShown.removeDuplicates();
    manualTags.sort(Qt::CaseInsensitive);
    aiShown.sort(Qt::CaseInsensitive);

    auto &lm = LanguageManager::instance();
    auto displayTag = [&](const QString &rawTag) {
        // Presentation-only translation: keep emoji/prefix (can be multi-QChar), translate base part if known.
        QString t = rawTag.trimmed();
        if (t.isEmpty()) return t;

        QString prefix;
        int i = 0;
        while (i < t.size()) {
            const QChar c = t.at(i);
            if (c.isSpace()) break;
            if (c.isLetterOrNumber()) break;
            prefix.append(c);
            ++i;
        }
        const QString rest = t.mid(i).trimmed();
        if (!prefix.isEmpty() && !rest.isEmpty()) {
            const QString translatedRest = lm.getText(rest);
            if (translatedRest != rest) return QStringLiteral("%1 %2").arg(prefix, translatedRest).trimmed();
        }

        return lm.getText(t);
    };

    QString html;
    html += QStringLiteral("<div><b>%1</b>: ").arg(displayTag(QStringLiteral("個人標籤")).toHtmlEscaped());
    if (manualTags.isEmpty()) {
        html += QStringLiteral("<span style='color:#888'>(%1)</span>").arg(displayTag(QStringLiteral("無")).toHtmlEscaped());
    } else {
        for (const QString &t : manualTags) {
            html += QStringLiteral("<span style='background:#444; color:#fff; padding:2px 6px; border-radius:8px; margin-right:6px;'>%1</span>")
                        .arg(displayTag(t).toHtmlEscaped());
        }
    }
    html += QStringLiteral("</div>");

    html += QStringLiteral("<div style='margin-top:6px;'><b>%1</b>: ")
                .arg(displayTag(QStringLiteral("AI 智能建議")).toHtmlEscaped());
    if (aiShown.isEmpty()) {
        html += QStringLiteral("<span style='color:#888'>(%1)</span>").arg(displayTag(QStringLiteral("無")).toHtmlEscaped());
    } else {
        for (const QString &t : aiShown) {
            html += QStringLiteral("<span style='background:#2b6cb0; color:#fff; padding:2px 6px; border-radius:8px; margin-right:6px;'>🤖 %1</span>")
                        .arg(displayTag(t).toHtmlEscaped());
        }
    }
    html += QStringLiteral("</div>");

    lblTags->setText(html);
}

QString MainWindow::historicalTagsString() const {
    std::vector<QString> tags;
    {
        QMutexLocker locker(&tagMutex);
        tags = tagManager.getAllTags();
    }
    QStringList parts;
    for (const auto &t : tags) parts << t;
    return parts.join(QStringLiteral(", "));
}

std::vector<QString> MainWindow::sanitizeAiTags(const QString &raw) const {
    QString cleaned = raw;
    cleaned.replace(QRegularExpression(QStringLiteral("System:|Assistant:|User:|輸出:|標籤:|標签:"), QRegularExpression::CaseInsensitiveOption), QString());
    cleaned.replace(QStringLiteral("\n"), QStringLiteral(" ")).replace(QStringLiteral("\r"), QStringLiteral(" "));

    QStringList parts = cleaned.split(QRegularExpression(QStringLiteral("[,，、]")), Qt::SkipEmptyParts);
    QSet<QString> seen;
    std::vector<QString> out;
    const bool en = (LanguageManager::instance().language() == LanguageManager::Language::EN_US);
    const int maxLen = en ? 24 : 8;

    for (const QString &p0 : parts) {
        QString p = p0.trimmed();
        p.replace(QRegularExpression(QStringLiteral("[\\s\\.。;；:：\\[\\]\\(\\)<>\"'`~!@#$%^&*+=\\|\\\\/?]+")), QString());
        if (p.isEmpty()) continue;
        if (p.size() > maxLen) continue;
        if (seen.contains(p)) continue;
        seen.insert(p);
        out.push_back(p);
        if (out.size() >= 5) break;
    }
    return out;
}

void MainWindow::setUiBusy(bool busy) {
    btnAnalyzeFile->setEnabled(!busy && !currentFilePath().isEmpty());
    btnCancelAnalysis->setEnabled(busy);
    btnSaveTags->setEnabled(!busy && btnSaveTags->isEnabled());
    fileList->setEnabled(!busy);
    folderTree->setEnabled(!busy);
    tagListWidget->setEnabled(!busy);
    cmbTagFilter->setEnabled(!busy);
    txtSearch->setEnabled(!busy);
    cmbSort->setEnabled(!busy);
    btnHome->setEnabled(!busy);
    if (!busy) syncNavigationButtons();
    else {
        btnBack->setEnabled(false);
        btnForward->setEnabled(false);
    }
}

void MainWindow::analyzeFile() {
    const QString fp = currentFilePath();
    if (fp.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("請先選擇檔案"));
        return;
    }

    QFileInfo fi(fp);
    if (!isAnalyzableFile(fi)) {
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("此項目不可分析")));
        return;
    }

    if (!llamaEngine.isModelLoaded()) {
        // If we're auto-loading in background, wait (blocking) to avoid "Model not loaded" race.
        if (modelLoadWatcher && modelLoadWatcher->isRunning()) {
            lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("等待模型載入完成…")));
            modelLoadWatcher->future().waitForFinished();
        }
        if (!llamaEngine.isModelLoaded()) {
            QMessageBox::warning(this, QStringLiteral("Model"), QStringLiteral("模型尚未載入"));
            return;
        }
    }

    cancelFlag.store(false);
    setUiBusy(true);
    lblStatus->setText(tr("Preparing…"));

    const QString filename = fi.fileName();
    const QString existingTags = historicalTagsString();
    const QString rejectedTagsCsv = [this]() {
        QMutexLocker locker(&tagMutex);
        return tagManager.getRejectedTags().join(QStringLiteral(", "));
    }();

    std::string content;
    const QString suffix = fi.suffix().toLower();
    if (suffix == QStringLiteral("pdf") || suffix == QStringLiteral("docx") || suffix == QStringLiteral("xlsx")) {
        content = DocumentParser::extractText(fp.toStdString());
        if (content.size() > 2500) content = content.substr(0, 2500);
    } else {
        std::ifstream f(fp.toStdString(), std::ios::binary);
        if (f.is_open()) {
            std::string buf;
            buf.resize(2048);
            f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            buf.resize(static_cast<size_t>(f.gcount()));
            const QString q = QString::fromUtf8(buf.data(), static_cast<int>(buf.size()));
            if (!q.isEmpty()) content = q.toStdString();
        }
    }

    lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析中…")));

    QFuture<std::string> future = QtConcurrent::run([this, filename, content, existingTags, rejectedTagsCsv]() {
        // existingTags param is used for "historical tags"; rejectedTagsCsv used to constrain outputs
        return llamaEngine.suggestTags(filename.toStdString(), content, rejectedTagsCsv.toStdString(), existingTags.toStdString());
    });
    watcher->setFuture(future);
}

void MainWindow::cancelAnalysis() {
    cancelFlag.store(true);
    lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("取消中…")));
}

void MainWindow::onAnalysisFinished() {
    setUiBusy(false);

    const QString fp = currentFilePath();
    const std::string raw = watcher->result();
    const QString qRaw = QString::fromStdString(raw);

    if (raw.rfind("Error:", 0) == 0) {
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析失敗")));
        QMessageBox::critical(this, QStringLiteral("Error"), qRaw);
        return;
    }

    if (cancelFlag.load()) {
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("已取消")));
        return;
    }

    const auto tags = sanitizeAiTags(qRaw);
    if (fp.isEmpty()) {
        lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析完成（無選取檔案）")));
        return;
    }

    {
        QMutexLocker locker(&tagMutex);
        for (const auto &t : tags) tagManager.addTag(fp, QStringLiteral("[AI] ") + t, false);
        tagManager.saveTags();
    }

    updateTagDisplayForFile(fp);
    updateTagList();
    if (fileListMode == FileListMode::PhysicalFolder) scanPhysicalFolder();
    else populateVirtualTagFiles(activeVirtualTag);

    lblStatus->setText(LanguageManager::instance().getText(QStringLiteral("分析完成")));
}

void MainWindow::saveTags() {
    const QString fp = currentFilePath();
    if (fp.isEmpty()) return;
    {
        QMutexLocker locker(&tagMutex);
        tagManager.saveTags();
    }
    lblStatus->setText(QStringLiteral("已儲存"));
    btnSaveTags->setEnabled(false);
}

void MainWindow::addTag() {
    const QString fp = currentFilePath();
    if (fp.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("請先選擇檔案"));
        return;
    }
    bool ok = false;
    const QString t = QInputDialog::getText(this, QStringLiteral("Add Tag"), QStringLiteral("標籤:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || t.isEmpty()) return;

    {
        QMutexLocker locker(&tagMutex);
        tagManager.addTag(fp, t, true);
        tagManager.saveTags();
    }
    updateTagDisplayForFile(fp);
    updateTagList();
    if (fileListMode == FileListMode::PhysicalFolder) scanPhysicalFolder();
    else populateVirtualTagFiles(activeVirtualTag);
}

void MainWindow::removeTag() {
    const QString fp = currentFilePath();
    if (fp.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("請先選擇檔案"));
        return;
    }

    std::vector<QString> tags;
    {
        QMutexLocker locker(&tagMutex);
        tags = tagManager.getTags(fp);
    }
    if (tags.empty()) {
        QMessageBox::information(this, QStringLiteral("Info"), QStringLiteral("無標籤"));
        return;
    }

    QStringList items;
    for (const auto &t : tags) items << t;

    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, QStringLiteral("Remove"), QStringLiteral("選擇要移除的標籤:"), items, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    {
        QMutexLocker locker(&tagMutex);
        tagManager.removeTag(fp, chosen);
        tagManager.addRejectedTag(chosen);
        tagManager.saveTags();
    }
    updateTagDisplayForFile(fp);
    updateTagList();
    if (fileListMode == FileListMode::PhysicalFolder) scanPhysicalFolder();
    else populateVirtualTagFiles(activeVirtualTag);
}

void MainWindow::removeGlobalTag() {
    const auto selected = tagListWidget->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("請先選擇標籤"));
        return;
    }

    const QString data = selected.first()->data(Qt::UserRole).toString();
    if (data == QStringLiteral("ALL")) {
        QMessageBox::warning(this, tr("Warning"), tr("Cannot delete All Files"));
        return;
    }

    const QString tag = normalizeDisplayTag(data);
    const auto reply = QMessageBox::question(this, QStringLiteral("Delete"),
                                            QStringLiteral("確定刪除標籤「%1」？").arg(tag),
                                            QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    {
        QMutexLocker locker(&tagMutex);
        tagManager.deleteTag(tag);
        tagManager.addRejectedTag(tag);
        tagManager.saveTags();
    }
    fileListMode = FileListMode::PhysicalFolder;
    activeVirtualTag.clear();
    updateTagList();
    scanFiles();
}

void MainWindow::rebuildAddExistingTagMenu() {
    auto *menu = new QMenu(this);

    std::vector<QString> allTags;
    {
        QMutexLocker locker(&tagMutex);
        allTags = tagManager.getAllTags();
    }
    QStringList history;
    for (const auto &t : allTags) history << normalizeDisplayTag(t);

    auto addCategory = [&](const QString &name, const QStringList &preset) {
        QMenu *sub = menu->addMenu(LanguageManager::instance().getText(name));
        for (const QString &t : preset) {
            const QString canon = normalizeDisplayTag(t);
            const QString baseZh = systemTagBaseZh(canon).isEmpty() ? canon : systemTagBaseZh(canon);
            const QString emoji = systemTagEmojiPrefix(canon);
            const QString display = (LanguageManager::instance().getText(baseZh) == baseZh)
                                        ? canon
                                        : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
            QAction *a = sub->addAction(display.trimmed());
            a->setData(canon);
            connect(a, &QAction::triggered, this, [this, a]() {
                const QString fp = currentFilePath();
                if (fp.isEmpty()) return;
                const QString tag = a->data().toString();
                {
                    QMutexLocker locker(&tagMutex);
                    tagManager.addTag(fp, tag, true);
                    tagManager.saveTags();
                }
                updateTagDisplayForFile(fp);
                updateTagList();
                if (fileListMode == FileListMode::PhysicalFolder) scanPhysicalFolder();
                else populateVirtualTagFiles(activeVirtualTag);
            });
        }
        if (!history.isEmpty()) {
            sub->addSeparator();
            for (const QString &t : history) {
                const QString canon = normalizeDisplayTag(t);
                const QString baseZh = systemTagBaseZh(canon).isEmpty() ? canon : systemTagBaseZh(canon);
                const QString emoji = systemTagEmojiPrefix(canon);
                const QString display = (LanguageManager::instance().getText(baseZh) == baseZh)
                                            ? canon
                                            : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
                QAction *a = sub->addAction(display.trimmed());
                a->setData(canon);
                connect(a, &QAction::triggered, this, [this, a]() {
                    const QString fp = currentFilePath();
                    if (fp.isEmpty()) return;
                    const QString tag = a->data().toString();
                    {
                        QMutexLocker locker(&tagMutex);
                        tagManager.addTag(fp, tag, true);
                        tagManager.saveTags();
                    }
                    updateTagDisplayForFile(fp);
                    updateTagList();
                    if (fileListMode == FileListMode::PhysicalFolder) scanPhysicalFolder();
                    else populateVirtualTagFiles(activeVirtualTag);
                });
            }
        }
    };

    addCategory(QStringLiteral("🖼️ 圖片"), {QStringLiteral("相片"), QStringLiteral("截圖")});
    addCategory(QStringLiteral("🎬 影片"), {QStringLiteral("剪輯"), QStringLiteral("錄影")});
    addCategory(QStringLiteral("🎧 音訊"), {QStringLiteral("音樂"), QStringLiteral("錄音")});
    addCategory(QStringLiteral("📄 文件"), {QStringLiteral("報告"), QStringLiteral("簡報")});
    addCategory(QStringLiteral("📦 壓縮檔"), {QStringLiteral("備份"), QStringLiteral("打包")});
    addCategory(QStringLiteral("🧩 專案"), {QStringLiteral("程式碼"), QStringLiteral("研究")});

    btnAddExistingTag->setMenu(menu);
}

QStringList MainWindow::getFastPathTags(const QString &filename) {
    QStringList tags;
    const QString lower = filename.toLower();

    if (lower.contains(QStringLiteral("hw")) || lower.contains(QStringLiteral("homework")) || lower.contains(QStringLiteral("作業")) || lower.contains(QStringLiteral("報告")))
        tags << QStringLiteral("🎒學校作業");
    if (lower.contains(QStringLiteral("receipt")) || lower.contains(QStringLiteral("invoice")) || lower.contains(QStringLiteral("收據")) || lower.contains(QStringLiteral("發票")))
        tags << QStringLiteral("💰財務");
    if (lower.contains(QStringLiteral("setup")) || lower.contains(QStringLiteral("install")) || lower.contains(QStringLiteral("installer")) || lower.contains(QStringLiteral("安裝")))
        tags << QStringLiteral("💻安裝檔");
    if (lower.contains(QStringLiteral("backup")) || lower.contains(QStringLiteral("備份"))) tags << QStringLiteral("📦備份檔");
    if (lower.contains(QStringLiteral("meeting")) || lower.contains(QStringLiteral("會議"))) tags << QStringLiteral("🗓️會議");
    if (lower.contains(QStringLiteral("resume")) || lower.contains(QStringLiteral("cv")) || lower.contains(QStringLiteral("履歷"))) tags << QStringLiteral("🧑‍💼履歷");

    if (lower.endsWith(QStringLiteral(".exe")) || lower.endsWith(QStringLiteral(".dmg")) || lower.endsWith(QStringLiteral(".pkg")) || lower.endsWith(QStringLiteral(".msi")))
        tags << QStringLiteral("💻應用程式");
    if (lower.endsWith(QStringLiteral(".cpp")) || lower.endsWith(QStringLiteral(".h")) || lower.endsWith(QStringLiteral(".hpp")) || lower.endsWith(QStringLiteral(".c")) || lower.endsWith(QStringLiteral(".rs")) || lower.endsWith(QStringLiteral(".go")) || lower.endsWith(QStringLiteral(".py")) || lower.endsWith(QStringLiteral(".js")) || lower.endsWith(QStringLiteral(".ts")) || lower.endsWith(QStringLiteral(".java")) || lower.endsWith(QStringLiteral(".cs")))
        tags << QStringLiteral("⌨️程式碼");
    if (lower.endsWith(QStringLiteral(".pdf")) || lower.endsWith(QStringLiteral(".docx")) || lower.endsWith(QStringLiteral(".xlsx")) || lower.endsWith(QStringLiteral(".pptx")) || lower.endsWith(QStringLiteral(".txt")) || lower.endsWith(QStringLiteral(".md")) || lower.endsWith(QStringLiteral(".rtf")))
        tags << kTagDoc;
    if (lower.endsWith(QStringLiteral(".jpg")) || lower.endsWith(QStringLiteral(".jpeg")) || lower.endsWith(QStringLiteral(".png")) || lower.endsWith(QStringLiteral(".gif")) || lower.endsWith(QStringLiteral(".webp")) || lower.endsWith(QStringLiteral(".heic")) || lower.endsWith(QStringLiteral(".bmp")))
        tags << kTagImage;
    if (lower.endsWith(QStringLiteral(".mp4")) || lower.endsWith(QStringLiteral(".mov")) || lower.endsWith(QStringLiteral(".mkv")) || lower.endsWith(QStringLiteral(".avi")) || lower.endsWith(QStringLiteral(".webm")))
        tags << kTagVideo;
    if (lower.endsWith(QStringLiteral(".mp3")) || lower.endsWith(QStringLiteral(".wav")) || lower.endsWith(QStringLiteral(".m4a")) || lower.endsWith(QStringLiteral(".flac")) || lower.endsWith(QStringLiteral(".aac")))
        tags << kTagAudio;
    if (lower.endsWith(QStringLiteral(".zip")) || lower.endsWith(QStringLiteral(".rar")) || lower.endsWith(QStringLiteral(".7z")) || lower.endsWith(QStringLiteral(".tar")) || lower.endsWith(QStringLiteral(".gz")))
        tags << QStringLiteral("📦壓縮檔");
    if (lower.endsWith(QStringLiteral(".json")) || lower.endsWith(QStringLiteral(".xml")) || lower.endsWith(QStringLiteral(".yaml")) || lower.endsWith(QStringLiteral(".yml")) || lower.endsWith(QStringLiteral(".toml")) || lower.endsWith(QStringLiteral(".ini")))
        tags << QStringLiteral("🧩設定");
    if (lower.endsWith(QStringLiteral(".blend")) || lower.endsWith(QStringLiteral(".psd")) || lower.endsWith(QStringLiteral(".ai"))) tags << QStringLiteral("🎨設計");
    if (lower.endsWith(QStringLiteral(".sqlite")) || lower.endsWith(QStringLiteral(".db"))) tags << QStringLiteral("🗄️資料庫");

    tags.removeDuplicates();
    return tags;
}

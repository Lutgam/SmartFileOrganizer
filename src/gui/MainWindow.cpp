#include "MainWindow.h"
#include "../core/DocumentParser.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
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

#include <algorithm>
#include <fstream>
#include <map>

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
    setupFourColumnLayout();
    setupContextMenus();

    watcher = new QFutureWatcher<std::string>(this);
    connect(watcher, &QFutureWatcher<std::string>::finished, this, &MainWindow::onAnalysisFinished);

    modelLoadWatcher = new QFutureWatcher<bool>(this);
    connect(modelLoadWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        const bool ok = modelLoadWatcher->result();
        lblStatus->setText(ok ? QStringLiteral("模型已自動載入 (Model auto-loaded)")
                              : QStringLiteral("模型自動載入失敗 (Auto-load failed)"));
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
        lblStatus->setText(QStringLiteral("❌ 找不到模型: %1（請確認 assets/models/chat_model.gguf）").arg(modelPath));
    } else {
        lblStatus->setText(QStringLiteral("正在自動載入模型… %1").arg(modelPath));
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
}

MainWindow::~MainWindow() = default;

void MainWindow::onBackgroundScanProgress() {
    updateTagListCountsOnly();
}

void MainWindow::onBackgroundScanFinished() {
    updateTagList();
    lblStatus->setText(lblStatus->text() + QStringLiteral(" | 背景全域掃描完成"));
}

void MainWindow::setupToolbar() {
    toolbar = addToolBar(QStringLiteral("Main Toolbar"));
    toolbar->setMovable(false);
    QAction *actOpen = toolbar->addAction(QStringLiteral("開啟資料夾"));
    connect(actOpen, &QAction::triggered, this, &MainWindow::openFolder);
    toolbar->addSeparator();
}

void MainWindow::setupFourColumnLayout() {
    mainSplitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(mainSplitter);

    // --- Column 1: Tags ---
    tagsPanel = new QWidget(this);
    auto *tagsLayout = new QVBoxLayout(tagsPanel);
    auto *tagsHeader = new QHBoxLayout();
    tagsHeader->addWidget(new QLabel(QStringLiteral("🏷️ 標籤庫"), this));
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
    foldersLayout->addWidget(new QLabel(QStringLiteral("🗂️ 資料夾樹"), this));

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
    folderTree->setModel(folderModel);
    for (int col = 1; col < folderModel->columnCount(); ++col) folderTree->hideColumn(col);

    foldersLayout->addWidget(folderTree);
    connect(folderTree, &QTreeView::clicked, this, [this](const QModelIndex &idx) {
        if (!idx.isValid()) return;
        const QString selectedDir = folderModel->filePath(idx);
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
    filesLayout->addWidget(new QLabel(QStringLiteral("📂 檔案清單"), this));

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
    cmbTagFilter->addItem(QStringLiteral("All Files"));
    cmbTagFilter->setToolTip(QStringLiteral("🏷️ 標籤篩選"));
    rowFilter->addWidget(cmbTagFilter, 1);

    txtSearch = new QLineEdit(this);
    txtSearch->setPlaceholderText(QStringLiteral("🔍 搜尋"));
    rowFilter->addWidget(txtSearch, 2);
    controlsCol->addLayout(rowFilter);

    filesLayout->addLayout(controlsCol);

    fileList = new QListWidget(this);
    fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fileList, &QListWidget::itemClicked, this, &MainWindow::onFileSelected);
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
    previewLayout->addWidget(new QLabel(QStringLiteral("👁️ 預覽與控制"), this));

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

    auto *btnRow1 = new QHBoxLayout();
    btnAnalyzeFile = new QPushButton(QStringLiteral("✨ 分析"), this);
    connect(btnAnalyzeFile, &QPushButton::clicked, this, &MainWindow::analyzeFile);
    btnRow1->addWidget(btnAnalyzeFile);

    btnCancelAnalysis = new QPushButton(QStringLiteral("⛔ 取消"), this);
    connect(btnCancelAnalysis, &QPushButton::clicked, this, &MainWindow::cancelAnalysis);
    btnCancelAnalysis->setEnabled(false);
    btnRow1->addWidget(btnCancelAnalysis);

    btnSaveTags = new QPushButton(QStringLiteral("💾 儲存"), this);
    connect(btnSaveTags, &QPushButton::clicked, this, &MainWindow::saveTags);
    btnSaveTags->setEnabled(false);
    btnRow1->addWidget(btnSaveTags);
    previewLayout->addLayout(btnRow1);

    auto *btnRow2 = new QHBoxLayout();
    btnAddTag = new QPushButton(QStringLiteral("➕ 新增"), this);
    connect(btnAddTag, &QPushButton::clicked, this, &MainWindow::addTag);
    btnRow2->addWidget(btnAddTag);

    btnRemoveTag = new QPushButton(QStringLiteral("➖ 移除"), this);
    connect(btnRemoveTag, &QPushButton::clicked, this, &MainWindow::removeTag);
    btnRow2->addWidget(btnRemoveTag);
    previewLayout->addLayout(btnRow2);

    btnAddExistingTag = new QPushButton(QStringLiteral("🏷️ 加入現有標籤"), this);
    previewLayout->addWidget(btnAddExistingTag);
    rebuildAddExistingTagMenu();

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

    connect(fileList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *it = fileList->itemAt(pos);
        if (!it) return;
        const QString fp = it->data(Qt::UserRole).toString();
        if (fp.isEmpty()) return;

        std::vector<QString> allTags;
        {
            QMutexLocker locker(&tagMutex);
            allTags = tagManager.getAllTags();
        }

        QMenu menu(this);
        QMenu *sub = menu.addMenu(QStringLiteral("指定標籤"));
        for (const auto &t : allTags) {
            QAction *a = sub->addAction(t);
            connect(a, &QAction::triggered, this, [this, fp, t]() {
                QMutexLocker locker(&tagMutex);
                tagManager.addTag(fp, t, true);
                tagManager.saveTags();
                updateTagDisplayForFile(fp);
                updateTagList();
                if (fileListMode == FileListMode::PhysicalFolder) scanFiles();
                else populateVirtualTagFiles(activeVirtualTag);
            });
        }
        menu.exec(fileList->mapToGlobal(pos));
    });
}

void MainWindow::mapsHomeFixAndSetRoot(const QString &dir) {
    QFileInfo fi(dir);
    const QString abs = fi.exists() ? fi.absoluteFilePath() : QDir::homePath();
    rootPath = abs;
    currentPath = abs;

    folderModel->setRootPath(rootPath);
    setFolderTreeCurrentPath(rootPath);

    tagManager.loadTags(rootPath.toStdString());
}

void MainWindow::setFolderTreeCurrentPath(const QString &absDir) {
    const QModelIndex idx = folderModel->index(absDir);
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
    folderModel->setRootPath(rootPath);
    folderTree->setRootIndex(folderModel->index(home));
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
    folderTree->setRootIndex(folderModel->index(dir));
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
        return;
    }
    scanPhysicalFolder();
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

    lblStatus->setText(QStringLiteral("資料夾: %1 | 檔案數: %2 %3")
                           .arg(currentPath)
                           .arg(count)
                           .arg(recursive ? QStringLiteral("[遞迴]") : QStringLiteral("[僅此層]")));

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

    lblStatus->setText(QStringLiteral("虛擬標籤檢視: %1 | 檔案數: %2")
                           .arg(tag)
                           .arg(static_cast<int>(m_pendingFilesToDisplay.size())));

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
            const QString name = fi.fileName();
            const QString parent = parentDirDisplay(filePath);
            auto *item = new QListWidgetItem();
            item->setData(Qt::UserRole, filePath);

            auto *widget = new QWidget(fileList);
            auto *layout = new QHBoxLayout(widget);
            layout->setContentsMargins(5, 2, 5, 2);
            layout->setSpacing(8);

            auto *nameLabel = new QLabel(name, widget);
            layout->addWidget(nameLabel);

            auto *pathLabel = new QLabel(QStringLiteral("[%1]").arg(parent), widget);
            pathLabel->setStyleSheet(QStringLiteral("color: gray;"));
            layout->addWidget(pathLabel);

            layout->addStretch();

            item->setSizeHint(widget->sizeHint());
            fileList->addItem(item);
            fileList->setItemWidget(item, widget);
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
        it->setText(QStringLiteral("%1 (%2)").arg(canon).arg(n));
        it->setData(Qt::UserRole, canon);
    }
    syncTagFilterFromTagList();
}

void MainWindow::updateTagList() {
    tagListWidget->clear();

    auto *allItem = new QListWidgetItem(QStringLiteral("All Files"), tagListWidget);
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
        auto *it = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(canon).arg(n), tagListWidget);
        it->setData(Qt::UserRole, canon);
    }

    syncTagFilterFromTagList();
    rebuildAddExistingTagMenu();
}

void MainWindow::syncTagFilterFromTagList() {
    const QString prev = cmbTagFilter->currentText();
    cmbTagFilter->blockSignals(true);
    cmbTagFilter->clear();
    cmbTagFilter->addItem(QStringLiteral("All Files"));
    for (int i = 0; i < tagListWidget->count(); ++i) {
        const auto *it = tagListWidget->item(i);
        if (!it) continue;
        if (it->data(Qt::UserRole).toString() == QStringLiteral("ALL")) continue;
        const QString rawTag = it->data(Qt::UserRole).toString();
        if (rawTag.isEmpty()) continue;
        cmbTagFilter->addItem(rawTag, rawTag);
    }
    const int idx = cmbTagFilter->findText(prev);
    cmbTagFilter->setCurrentIndex(idx >= 0 ? idx : 0);
    cmbTagFilter->blockSignals(false);
}

void MainWindow::syncTagListFromTagFilter() {
    const QString selected = cmbTagFilter->currentText();
    if (selected == QStringLiteral("All Files")) {
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
    const QString tagFilter = cmbTagFilter->currentText();

    std::vector<QString> filesWithTag;
    if (tagFilter != QStringLiteral("All Files")) {
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

        if (match && tagFilter != QStringLiteral("All Files")) {
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
    txtPreviewText->setPlainText(typeLine + QStringLiteral("\n(二進位檔：不顯示內容)"));
}

void MainWindow::updateTagDisplayForFile(const QString &absPath) {
    std::vector<QString> tags;
    {
        QMutexLocker locker(&tagMutex);
        tags = tagManager.getTags(absPath);
    }
    QString s = QStringLiteral("標籤: ");
    if (tags.empty()) {
        s += QStringLiteral("(無)");
    } else {
        for (int i = 0; i < static_cast<int>(tags.size()); ++i) {
            s += tags[static_cast<size_t>(i)];
            if (i + 1 < static_cast<int>(tags.size())) s += QStringLiteral(", ");
        }
    }
    lblTags->setText(s);
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

    for (const QString &p0 : parts) {
        QString p = p0.trimmed();
        p.replace(QRegularExpression(QStringLiteral("[\\s\\.。;；:：\\[\\]\\(\\)<>\"'`~!@#$%^&*+=\\|\\\\/?]+")), QString());
        if (p.isEmpty()) continue;
        if (p.size() > 8) continue;
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
        lblStatus->setText(QStringLiteral("此項目不可分析"));
        return;
    }

    if (!llamaEngine.isModelLoaded()) {
        QMessageBox::warning(this, QStringLiteral("Model"), QStringLiteral("模型尚未載入"));
        return;
    }

    cancelFlag.store(false);
    setUiBusy(true);
    lblStatus->setText(QStringLiteral("準備分析…"));

    const QString filename = fi.fileName();
    const QString existingTags = historicalTagsString();

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

    lblStatus->setText(QStringLiteral("分析中…"));

    QFuture<std::string> future = QtConcurrent::run([this, filename, content, existingTags]() {
        return llamaEngine.suggestTags(filename.toStdString(), content, existingTags.toStdString());
    });
    watcher->setFuture(future);
}

void MainWindow::cancelAnalysis() {
    cancelFlag.store(true);
    lblStatus->setText(QStringLiteral("取消中…"));
}

void MainWindow::onAnalysisFinished() {
    setUiBusy(false);

    const QString fp = currentFilePath();
    const std::string raw = watcher->result();
    const QString qRaw = QString::fromStdString(raw);

    if (raw.rfind("Error:", 0) == 0) {
        lblStatus->setText(QStringLiteral("分析失敗"));
        QMessageBox::critical(this, QStringLiteral("Error"), qRaw);
        return;
    }

    if (cancelFlag.load()) {
        lblStatus->setText(QStringLiteral("已取消"));
        return;
    }

    const auto tags = sanitizeAiTags(qRaw);
    if (fp.isEmpty()) {
        lblStatus->setText(QStringLiteral("分析完成（無選取檔案）"));
        return;
    }

    {
        QMutexLocker locker(&tagMutex);
        for (const auto &t : tags) tagManager.addTag(fp, t, false);
        tagManager.saveTags();
    }

    updateTagDisplayForFile(fp);
    updateTagList();
    if (fileListMode == FileListMode::PhysicalFolder) scanPhysicalFolder();
    else populateVirtualTagFiles(activeVirtualTag);

    lblStatus->setText(QStringLiteral("分析完成"));
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
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("無法刪除 All Files"));
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
        QMenu *sub = menu->addMenu(name);
        for (const QString &t : preset) {
            QAction *a = sub->addAction(t);
            connect(a, &QAction::triggered, this, [this, t]() {
                const QString fp = currentFilePath();
                if (fp.isEmpty()) return;
                {
                    QMutexLocker locker(&tagMutex);
                    tagManager.addTag(fp, t, true);
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
                QAction *a = sub->addAction(t);
                connect(a, &QAction::triggered, this, [this, t]() {
                    const QString fp = currentFilePath();
                    if (fp.isEmpty()) return;
                    {
                        QMutexLocker locker(&tagMutex);
                        tagManager.addTag(fp, t, true);
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

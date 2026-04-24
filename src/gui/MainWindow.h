#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QCheckBox>
#include <QComboBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QToolBar>
#include <QTreeView>
#include <QtConcurrent>

#include <QFileSystemModel>
#include <QStringList>
#include <QMutex>
#include <QVector>

#include <atomic>
#include <string>
#include <vector>

#include "../ai/LlamaEngine.h"
#include "../core/TagManager.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void openFolder();
    void scanFiles();
    void filterFiles();
    void onFileSelected(QListWidgetItem *item);
    void onTagSelected(QListWidgetItem *item);

    void goBack();
    void goForward();
    void goHome();
    void onSortChanged(int index);

    void analyzeFile();
    void cancelAnalysis();
    void onAnalysisFinished();

    void saveTags();
    void addTag();
    void removeTag();
    void removeGlobalTag();

    void onBackgroundScanProgress();
    void onBackgroundScanFinished();

private:
    enum class FileListMode { PhysicalFolder, VirtualTag };

    // ===== Layout =====
    QSplitter *mainSplitter = nullptr;

    // Column 1: Tags
    QWidget *tagsPanel = nullptr;
    QListWidget *tagListWidget = nullptr;
    QPushButton *btnLeftAddTag = nullptr;
    QPushButton *btnLeftRemoveTag = nullptr;

    // Column 2: Folders + navigation under title
    QWidget *foldersPanel = nullptr;
    QTreeView *folderTree = nullptr;
    QFileSystemModel *folderModel = nullptr;
    QPushButton *btnBack = nullptr;
    QPushButton *btnForward = nullptr;
    QPushButton *btnHome = nullptr;

    // Column 3: Files
    QWidget *filesPanel = nullptr;
    QComboBox *cmbSort = nullptr;
    QComboBox *cmbTagFilter = nullptr;
    QLineEdit *txtSearch = nullptr;
    QListWidget *fileList = nullptr;
    QPushButton *btnLoadMore = nullptr;
    QPushButton *btnLoadAll = nullptr;

    // Column 4: Preview & Controls
    QWidget *previewPanel = nullptr;
    QLabel *lblPreviewImage = nullptr;
    QTextEdit *txtPreviewText = nullptr;
    QLabel *lblTags = nullptr;
    QLabel *lblStatus = nullptr;

    QPushButton *btnAnalyzeFile = nullptr;
    QPushButton *btnCancelAnalysis = nullptr;
    QPushButton *btnSaveTags = nullptr;
    QPushButton *btnAddTag = nullptr;
    QPushButton *btnRemoveTag = nullptr;
    QPushButton *btnAddExistingTag = nullptr;

    QToolBar *toolbar = nullptr;
    QCheckBox *chkRecursive = nullptr;

    QString rootPath;
    QString currentPath;

    TagManager tagManager;
    LlamaEngine llamaEngine;

    QFutureWatcher<std::string> *watcher = nullptr;
    QFutureWatcher<bool> *modelLoadWatcher = nullptr;
    QFutureWatcher<void> *initialScanWatcher = nullptr;
    std::atomic<bool> cancelFlag{false};
    std::atomic<int> backgroundScanProgress{0};

    mutable QMutex tagMutex;

    FileListMode fileListMode = FileListMode::PhysicalFolder;
    QString activeVirtualTag;

    std::vector<QString> m_pendingFilesToDisplay;
    int m_currentLoadedCount = 0;
    static constexpr int BATCH_SIZE = 200;

    QVector<QString> navHistory;
    int navIndex = -1;

    QString currentFilePath() const;

    void setupToolbar();
    void setupFourColumnLayout();
    void setupContextMenus();

    void mapsHomeFixAndSetRoot(const QString &dir);
    void setFolderTreeCurrentPath(const QString &absDir);
    void pushHistory(const QString &path);
    void navigateToFolder(const QString &path, bool pushToHistory);
    void syncNavigationButtons();

    void scanPhysicalFolder();
    void populateVirtualTagFiles(const QString &tag);
    void renderFileListBatch(int count);
    void sortFileList();

    void updateTagList();
    void updateTagListCountsOnly();
    void syncTagFilterFromTagList();
    void syncTagListFromTagFilter();
    void setUiBusy(bool busy);

    void updatePreviewForFile(const QString &absPath);
    void updateTagDisplayForFile(const QString &absPath);

    void rebuildAddExistingTagMenu();
    QString historicalTagsString() const;
    std::vector<QString> sanitizeAiTags(const QString &raw) const;
    QStringList getFastPathTags(const QString &filename);

    bool isAnalyzableFile(const QFileInfo &fi) const;
};

#endif // MAINWINDOW_H

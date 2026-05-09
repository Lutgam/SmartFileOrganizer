#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QCheckBox>
#include <QComboBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QList>
#include <QGroupBox>
#include <QMenu>
#include <QPair>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QToolBar>
#include <QTreeView>
#include <QtConcurrent>

#include <QFileSystemModel>
#include <QSortFilterProxyModel>
#include <QFileInfo>
#include <QDir>
#include <QFileSystemWatcher>
#include <QTabWidget>
#include <QStringList>
#include <QMutex>
#include <QTimer>
#include <QVector>
#include <QHash>
#include <QQueue>
#include <QProgressBar>
#include <QMap>
#include <QPropertyAnimation>

#include <atomic>
#include <string>
#include <vector>

#include "../ai/LlamaEngine.h"
#include "../core/TagManager.h"

class WorkspaceFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    QString m_workspaceRoot;
    QString m_workspaceParent;

    explicit WorkspaceFilterProxyModel(QObject *parent = nullptr) : QSortFilterProxyModel(parent) {}

    void setWorkspace(const QString &path) {
        m_workspaceRoot = QDir::cleanPath(path);
        m_workspaceParent = QFileInfo(m_workspaceRoot).path();
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override {
        QFileSystemModel *fsModel = qobject_cast<QFileSystemModel *>(sourceModel());
        if (!fsModel || m_workspaceRoot.isEmpty()) return true;

        const QString parentPath = QDir::cleanPath(fsModel->filePath(sourceParent));

        // If listing is under the workspace parent, only allow the workspace itself.
        if (parentPath == m_workspaceParent) {
            const QModelIndex index = fsModel->index(sourceRow, 0, sourceParent);
            const QString itemPath = QDir::cleanPath(fsModel->filePath(index));
            return itemPath == m_workspaceRoot;
        }
        return true;
    }
};

class GraphWidget;
class DuplicateCleanerWidget;
class SettingsDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void openFolder();
    void scanFiles();
    void filterFiles();
    void showFileContextMenu(const QPoint &pos);
    void onFileSelected(QListWidgetItem *item);
    void onTagSelected(QListWidgetItem *item);

    void physicalArchiveFiles();
    void undoLastPhysicalArchive();
    void onDirectoryChanged(const QString &path);
    void showDuplicateCleanerTab();
    void onDuplicateCleanupCompleted(const QList<QPair<QString, QString>> &movedHistory);
    void openSettings();

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
    void consolidateTagsWithAI();
    void onConsolidateTagsFinished();

private:
    enum class FileListMode { PhysicalFolder, VirtualTag };

    // ===== Layout =====
    QSplitter *mainSplitter = nullptr;
    QTabWidget *m_mainTabWidget = nullptr;
    QWidget *m_workspaceTab = nullptr;
    QWidget *m_duplicateCleanerTab = nullptr;
    QWidget *m_graphTab = nullptr;
    DuplicateCleanerWidget *m_duplicateCleanerWidget = nullptr;
    GraphWidget *m_graphWidget = nullptr;

    QAction *m_actOpenFolder = nullptr;
    QAction *m_actFindDuplicates = nullptr;
    QAction *m_actSettings = nullptr;

    void updateAllTexts();

    // Column 1: Tags (tabbed: system / AI)
    QWidget *tagsPanel = nullptr;
    QLabel *lblTagLibraryTitle = nullptr;
    QTabWidget *m_tagTabWidget = nullptr;
    QListWidget *m_systemTagListWidget = nullptr;
    QListWidget *m_aiTagListWidget = nullptr;
    QPushButton *btnLeftAddTag = nullptr;
    QPushButton *btnLeftRemoveTag = nullptr;

    // Column 2: Folders + navigation under title
    QWidget *foldersPanel = nullptr;
    QLabel *lblFolderTreeTitle = nullptr;
    QTreeView *folderTree = nullptr;
    QLabel *workspaceTitleLabel = nullptr;
    QFileSystemModel *folderModel = nullptr;
    WorkspaceFilterProxyModel *proxyModel = nullptr;
    QPushButton *btnBack = nullptr;
    QPushButton *btnForward = nullptr;
    QPushButton *btnHome = nullptr;

    // Column 3: Files
    QWidget *filesPanel = nullptr;
    QLabel *lblFileListTitle = nullptr;
    QComboBox *cmbSort = nullptr;
    QComboBox *cmbTagFilter = nullptr;
    QLineEdit *txtSearch = nullptr;
    QListWidget *fileList = nullptr;
    QPushButton *btnLoadMore = nullptr;
    QPushButton *btnLoadAll = nullptr;

    // Column 4: Preview & Controls
    QWidget *previewPanel = nullptr;
    QLabel *lblPreviewTitle = nullptr;
    QLabel *lblPreviewImage = nullptr;
    QTextEdit *txtPreviewText = nullptr;
    QLabel *lblTags = nullptr;
    QLabel *lblStatus = nullptr;
    QLabel *m_lblSummaryTitle = nullptr;
    QTextEdit *m_aiSummaryEdit = nullptr;
    QPushButton *btnBatchAnalyze = nullptr;
    QPushButton *btnStopBatchAnalyze = nullptr;
    QProgressBar *batchProgressBar = nullptr;
    QLabel *lblBatchStatus = nullptr;
    QTabWidget *m_previewTabWidget = nullptr;
    QWidget *m_previewTagTab = nullptr;
    QWidget *m_previewOpsTab = nullptr;

    QPushButton *btnAnalyzeFile = nullptr;
    QPushButton *btnCancelAnalysis = nullptr;
    QPushButton *btnSaveTags = nullptr;
    QPushButton *btnAddTag = nullptr;
    QPushButton *btnRemoveTag = nullptr;
    QPushButton *btnAddExistingTag = nullptr;
    QPushButton *btnAutoMergeTags = nullptr;
    QPushButton *btnPhysicalArchive = nullptr;
    QPushButton *btnUndoPhysicalArchive = nullptr;
    // Tabbed UI: no duplicate/graph buttons in Tab 1 preview panel
    // Tag management + file operations are now in m_previewTabWidget

    QToolBar *toolbar = nullptr;
    QCheckBox *chkRecursive = nullptr;

    QString rootPath;
    QString currentPath;

    TagManager tagManager;
    LlamaEngine llamaEngine;

    QFutureWatcher<std::string> *watcher = nullptr;
    QFutureWatcher<std::string> *m_consolidateWatcher = nullptr;
    QFutureWatcher<bool> *modelLoadWatcher = nullptr;
    QFutureWatcher<void> *initialScanWatcher = nullptr;

    bool m_isConsolidatingTags = false;
    std::atomic<bool> cancelFlag{false};
    std::atomic<int> backgroundScanProgress{0};

    mutable QMutex tagMutex;

    FileListMode fileListMode = FileListMode::PhysicalFolder;
    QString activeVirtualTag;
    QHash<QString, QString> m_aiSummaryByPath;

    // ===== Batch AI analysis queue =====
    QQueue<QString> m_analysisQueue;
    int m_totalBatchSize = 0;
    bool m_isBatchMode = false;
    QString m_currentAnalyzingFile;
    QMap<QString, QJsonObject> m_pendingResults;
    QPropertyAnimation *m_batchProgressAnim = nullptr;

    void startBatchAnalysis();
    void processNextInQueue();
    void analyzeFileForPath(const QString &absPath);
    void flushPendingBatchResults();

    std::vector<QString> m_pendingFilesToDisplay;
    int m_currentLoadedCount = 0;
    static constexpr int BATCH_SIZE = 200;

    QVector<QString> navHistory;
    int navIndex = -1;

    // [newPath, oldPath] for last physicalArchiveFiles() run
    QList<QPair<QString, QString>> m_lastMoveHistory;

    QFileSystemWatcher *m_dirWatcher = nullptr;
    QTimer *m_dirDebounceTimer = nullptr;
    QString m_lastDirChangePath;

    // Graph is embedded as Tab 3 (no standalone window)

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

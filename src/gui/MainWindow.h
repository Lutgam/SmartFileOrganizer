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
#include <QTreeWidget>
#include <QtConcurrent>

#include <QFileSystemModel>
#include <QSortFilterProxyModel>
#include <QFileInfo>
#include <QDir>
#include <QFileSystemWatcher>
#include <QTabWidget>
#include <QStringList>
#include <QSet>
#include <QMutex>
#include <QTimer>
#include <QVector>
#include <QHash>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QQueue>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QScrollArea>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "../ai/LlamaEngine.h"
#include "../core/TagManager.h"
#include "../core/DrawerCategoryLut.h"

/// Result of a single-file LLM analysis job; `workspaceEpochAtSubmit` is captured on the UI thread when the
/// future is submitted so stale completions after a workspace switch can be discarded without touching disk.
struct SfAnalysisOutcome {
    std::string raw;
    quint64 workspaceEpochAtSubmit = 0;
};

struct SemanticSearchWorkerResult {
    QList<QString> pickedAbsolutePaths;
    QMap<int, QString> idToPathSnapshot;
    QSet<QString> validWorkspacePathsSnapshot;
    QString rawLlmText;
    /// Epoch captured when the search was started (UI thread); mismatched handlers must drop results silently.
    quint64 workspaceEpochAtSubmit = 0;
};

struct TagClusterWorkerResult {
    QHash<QString, QString> newAiTagToDrawerKey;
    QString parseError;
    QString rawLlmText;
    bool parseOk = false;
    bool rawIsLlmError = false;
};

class QStackedWidget;

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
class SettingsPanel;
class BusyChip;
class AiTagDropTreeWidget;

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
    void onAiTagTreeItemClicked(QTreeWidgetItem *item, int column);

    void physicalArchiveFiles();
    void undoLastPhysicalArchive();
    void onDirectoryChanged(const QString &path);
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

    void onHeroOmniboxReturnPressed();
    void onHeroSearchModeChanged();
    void syncAiTagHierarchyFromTree();
    void loadColdArchiveYearsSetting();

    void onBackgroundScanProgress();
    void onBackgroundScanFinished();
    void onStartAnalysisClicked();
    void generateTagFoldersWithAI();
    void onTagFolderClustersFinished();
    void onBackgroundAutoAnalyzeDebounce();

    void onWorkspaceClearAiCache();
    void onWorkspaceClearHashCache();
    void onWorkspaceFactoryReset();

    void onHeroOmniboxTextChanged(const QString &text);
    void onSemanticSearchFinished();
    void onAssignForceCategoryClicked();
    void onSettingsPanelApplied();

private:
    friend class AiTagDropTreeWidget;
    enum class FileListMode { PhysicalFolder, VirtualTag, SemanticResults };

    // ===== Layout =====
    QSplitter *mainSplitter = nullptr;
    QTabWidget *m_mainTabWidget = nullptr;
    QWidget *m_workspaceTab = nullptr;
    QWidget *m_settingsTab = nullptr;
    SettingsPanel *m_settingsPanel = nullptr;
    QWidget *m_graphTab = nullptr;
    QLabel *m_graphTacticalTitle = nullptr;
    QWidget *m_taskCenterTab = nullptr;
    GraphWidget *m_graphWidget = nullptr;

    QWidget *m_workspaceTopBar = nullptr;
    QLineEdit *m_heroOmnibox = nullptr;
    QComboBox *m_cmbSearchMode = nullptr;
    QPushButton *m_btnSemanticSearch = nullptr;
    BusyChip *m_heroSearchBusyChip = nullptr;
    QTimer *m_heroSemanticSpinTimer = nullptr;
    int m_heroSemanticSpinPhase = 0;

    void updateAllTexts();
    void runHeroSemanticSearchQuery();
    void clearSemanticSearchFilter();
    void disableSemanticOverlays();
    void refreshSemanticGlobalBanner();
    void populateSemanticResultFiles();
    void reloadCurrentFileListPanel();
    void updateFloatingQueueMonitor();
    void repositionFloatingQueueMonitor();
    void onSaveSemanticResultsAsAiCategory();
    void applyTagClusterDrawerUi_commit(QHash<QString, QString> newMap);
    void loadAiUiDrawerAssignments();
    void saveAiUiDrawerAssignments() const;
    QString aiUiDrawerStorePath() const;
    QString categoriesConfigPath() const;
    void reloadCategoriesConfigFromWorkspace();
    void rebuildForceCategoryCombo();
    void setHeroSemanticBusy(bool busy);

    QWidget *tagsPanel = nullptr;
    QLabel *lblTagLibraryTitle = nullptr;
    QTabWidget *m_tagTabWidget = nullptr;
    QPushButton *m_btnStartAnalysis = nullptr;
    QListWidget *m_systemTagListWidget = nullptr;
    QTreeWidget *m_aiTagTreeWidget = nullptr;
    QPushButton *btnLeftAddTag = nullptr;
    QPushButton *btnLeftRemoveTag = nullptr;

    // Column 2: Folders + navigation under title
    QWidget *foldersPanel = nullptr;
    QLabel *lblFolderTreeTitle = nullptr;
    QTreeView *folderTree = nullptr;
    QPushButton *m_btnWorkspacePicker = nullptr;
    QFileSystemModel *folderModel = nullptr;
    WorkspaceFilterProxyModel *proxyModel = nullptr;
    QPushButton *btnBack = nullptr;
    QPushButton *btnForward = nullptr;
    QPushButton *btnHome = nullptr;

    // Column 3: Files
    QWidget *filesPanel = nullptr;
    QLabel *lblFileListTitle = nullptr;
    QLabel *lblCurrentTarget = nullptr;
    QWidget *m_fileListTargetSlot = nullptr;
    QLabel *m_semanticGlobalBanner = nullptr;
    QPushButton *m_btnSaveSemanticResultsAsCategory = nullptr;
    QWidget *m_bgQueueFloatingMonitor = nullptr;
    QLabel *m_bgQueueFloatingMonitorLabel = nullptr;
    QLabel *lblBackgroundStatus = nullptr;
    QPushButton *m_btnRestartBackgroundAnalyze = nullptr;
    bool m_showRestartBackgroundPrompt = false;
    QComboBox *cmbSort = nullptr;
    QComboBox *cmbTagFilter = nullptr;
    QListWidget *fileList = nullptr;
    /// Index 0: file list; index 1: global semantic search progress placeholder.
    QStackedWidget *m_fileListPageStack = nullptr;
    QPushButton *btnLoadMore = nullptr;
    QPushButton *btnLoadAll = nullptr;
    QWidget *m_fileListProgressPanel = nullptr;

    // Column 4: Preview & Controls
    QWidget *previewPanel = nullptr;
    QLabel *lblPreviewTitle = nullptr;
    QLabel *lblPreviewImage = nullptr;
    QScrollArea *m_previewTextScroll = nullptr;
    QStackedWidget *m_previewBodyStack = nullptr;
    QTextEdit *txtPreviewText = nullptr;
    QTabWidget *m_previewInsightTabWidget = nullptr;
    QWidget *m_previewPersonalTagTab = nullptr;
    QWidget *m_previewAiSuggestTab = nullptr;
    QWidget *m_previewAiSummaryTab = nullptr;
    QTextEdit *m_personalTagsView = nullptr;
    QTextEdit *m_aiSuggestionsView = nullptr;
    QWidget *m_statusRow = nullptr;
    BusyChip *m_statusBusyChip = nullptr;
    QLabel *lblStatus = nullptr;
    QTextEdit *m_aiSummaryEdit = nullptr;
    QPushButton *btnBatchAnalyze = nullptr;
    QPushButton *btnStopBatchAnalyze = nullptr;
    QProgressBar *batchProgressBar = nullptr;
    QLabel *lblBatchStatus = nullptr;
    QTabWidget *m_previewTabWidget = nullptr;
    QWidget *m_previewTagTab = nullptr;
    QWidget *m_previewOpsTab = nullptr;

    QSplitter *m_taskCenterSplitter = nullptr;
    QLabel *m_taskCenterStatusLabel = nullptr;
    QProgressBar *m_taskCenterBatchProgress = nullptr;
    QTextEdit *m_backgroundLogEdit = nullptr;
    QTreeWidget *m_taskCenterRedundancyTree = nullptr;
    QPushButton *m_taskCenterCleanBtn = nullptr;

    /// Task Center: cumulative redundancy (never cleared on new background batches until user cleans).
    QMap<QString, QSet<QString>> m_persistRedundancyHash;
    QMap<QString, QSet<QString>> m_persistRedundancyName;
    int m_tcAccumFilesAnalyzed = 0;
    int m_tcAccumTagAdds = 0;

    QPushButton *btnAnalyzeFile = nullptr;
    QPushButton *btnCancelAnalysis = nullptr;
    QPushButton *btnSaveTags = nullptr;
    QPushButton *btnAddTag = nullptr;
    QPushButton *btnRemoveTag = nullptr;
    QPushButton *btnAddExistingTag = nullptr;
    QComboBox *m_cmbForceCategory = nullptr;
    QPushButton *m_btnAssignForceCategory = nullptr;
    QPushButton *btnAutoMergeTags = nullptr;
    QPushButton *btnPhysicalArchive = nullptr;
    QPushButton *btnUndoPhysicalArchive = nullptr;
    QLabel *m_lblPhysicalArchiveWarning = nullptr;
    // Tabbed UI: no duplicate/graph buttons in Tab 1 preview panel
    // Tag management + file operations are now in m_previewTabWidget

    QCheckBox *chkRecursive = nullptr;

    QString rootPath;
    QString currentPath;

    TagManager tagManager;
    LlamaEngine *m_llamaEngine = nullptr;

    QFutureWatcher<SfAnalysisOutcome> *watcher = nullptr;
    QFutureWatcher<TagClusterWorkerResult> *m_consolidateWatcher = nullptr;
    QFutureWatcher<SemanticSearchWorkerResult> *m_semanticSearchWatcher = nullptr;
    QFutureWatcher<bool> *modelLoadWatcher = nullptr;
    QFutureWatcher<void> *initialScanWatcher = nullptr;

    bool m_isConsolidatingTags = false;
    std::atomic<bool> cancelFlag{false};
    std::atomic<int> backgroundScanProgress{0};
    /// Incremented on primary workspace / root-folder switches; async completions must match or be dropped.
    std::atomic<uint64_t> m_workspaceEpoch{0};

    mutable QMutex tagMutex;

    FileListMode fileListMode = FileListMode::PhysicalFolder;
    QString activeVirtualTag;
    QHash<QString, QString> m_aiSummaryByPath;

    bool m_semanticFilterActive = false;
    QSet<QString> m_semanticVisiblePaths;
    QStringList m_semanticPickedPaths;
    QString m_semanticLockedQuery;
    QSet<QString> m_semanticValidWorkspacePaths;
    QMap<int, QString> m_semanticSearchIdToPath;
    QHash<QString, QString> m_aiTagToDrawerKey;
    SfDrawerCategoryLut m_categoryLut;
    /// While true, directory debounce must not refresh the file list (avoids racing semantic-search UI).
    bool m_semanticSearchUiApplying = false;
    /// After a full list rebuild, re-select this absolute path once it appears (paged loads).
    QString m_fileListReselectPendingPath;

    // ===== Batch AI analysis queue =====
    QQueue<QString> m_analysisQueue;
    int m_totalBatchSize = 0;
    bool m_isBatchMode = false;
    QString m_currentAnalyzingFile;
    QMap<QString, QJsonObject> m_pendingResults;
    QPropertyAnimation *m_batchProgressAnim = nullptr;

    /// SHA-256 (hex) → last successful analysis JSON { summary, tags[] } for duplicate-file fast path.
    QHash<QString, QJsonObject> m_analysisByContentHash;
    QMap<QString, QSet<QString>> m_batchHashToPaths;
    QMap<QString, QSet<QString>> m_batchNameConflictPaths;
    int m_batchCompletedCount = 0;
    int m_folderReportAiTagAdds = 0;
    bool m_batchTriggeredByBackgroundAuto = false;
    /// `m_workspaceEpoch` when the current batch began; used to reject stale `flushPendingBatchResults`.
    quint64 m_batchFlushWorkspaceEpoch = 0;
    /// Immediate parent folder name for the file currently processed in batch (status label).
    QString m_backgroundAnalyzeFolderLabel;
    QString m_pendingPrioritySingleFile;
    /// Shown in lblCurrentTarget after folder-tree prepend (cleared when batch ends).
    QString m_priorityFolderBannerPath;
    bool m_bgAutoAnalyzeEnabled = false;
    QTimer *m_bgAutoAnalyzeDebounce = nullptr;
    int m_bgAnalyzeQueueRetries = 0;
    bool m_systemFileBypassEnabled = true;
    int m_coldArchiveYears = 0;
    quint64 m_manualAnalysisPrepEpoch = 0;
    /// Paths that must skip cold-archive short-circuit (prepend / folder prepend).
    QSet<QString> m_coldArchiveBypassPaths;

    void startBatchAnalysis();
    void processNextInQueue();
    void analyzeFileForPath(const QString &absPath, bool forceColdArchiveBypass = false);
    void flushPendingBatchResults();
    void showFolderAnalysisReport();
    void applyCachedAnalysisForHashHit(const QString &fp, const QJsonObject &cached, const QString &contentHashHex);
    void beginBatchAnalysisUi();
    void loadBackgroundAutoAnalyzeSetting();

    void watchDirectoryRecursively(const QString &rootPath);
    void applyFilesystemWatchPolicy();
    void ensureRecursiveWatchCoversWorkspace();
    void primeAnalysisCacheFromDisk(const QString &sha256Hex);
    void purgeStaleAiCacheAfterMetadataLoad();

    void recordBatchPathForContentHash(const QString &hashHex, const QString &filePath);
    void noteSameNameDifferentHashConflicts(const QString &filePath, const QString &hashHex);
    void updateBackgroundStatusLabel();
    void appendTaskCenterLog(const QString &text);
    void mergeTaskCenterRedundancyBatch(int batchFilesAnalyzed,
                                        int batchNewTagAdds,
                                        const QMap<QString, QSet<QString>> &hashGroups,
                                        const QMap<QString, QSet<QString>> &nameGroups);
    void refreshTaskCenterRedundancyTreeUi();
    void pruneTaskCenterPersistentRedundancy(const QStringList &removedPaths);
    void onTaskCenterCleanClicked();
    void syncBatchProgressBars();
    void applyDualTrackBatchProgressVisibility();
    void refreshCurrentAnalysisTargetUi();
    void syncBatchAnalyzeButtonLabel();

    void startAnalysisSpinnerForPath(const QString &absPath);
    void stopAnalysisSpinner();
    void tickAnalysisSpinner();
    void refreshFileAndFolderAnalysisIndicators();
    void ensureAnalysisIndicatorTimer();
    bool reselectFileInList(const QString &absPath);
    void snapshotFileListSelectionForListRebuild();
    void tryRestoreFileListSelectionAfterBatchPaint(int totalPendingCount);
    void syncPreviewBusySpinner();
    void clearAnalysisWorkFlagsAndSyncUi();

    QTimer *m_analysisSpinTimer = nullptr;
    int m_analysisSpinPhase = 0;
    /// True from enqueue of LLM work until watcher completion (covers Preparing / disk read before isRunning()).
    bool m_analysisUiWorkActive = false;
    QString elideStatusLine(const QString &fullText, int pixelBudget) const;

    void prependSingleFileToAnalysisQueueFront(const QString &absPath);
    void enqueuePriorityAnalyzeForFileIfNeeded(const QString &absPath);
    void prependUnanalyzedFromFolderToAnalysisQueue(const QString &folderAbs);
    void prioritizeAnalysisPaths(QStringList &paths, const QString &focusFolderAbs);

    void collectUnanalyzedPathsFromWorkspace(int maxFiles, QStringList *out);
    void haltInFlightAnalysisWork();
    void updateStartAnalysisButtonUi();
    void startWorkspaceAnalysisQueue(const QStringList &paths);
    bool trySystemBypassPreset(const QFileInfo &fi, QString *summaryOut, QStringList *tagsOut) const;
    bool tryColdArchiveBypass(const QFileInfo &fi, bool forceLlm, QString *summaryOut, QStringList *tagsOut) const;
    void applyColdArchiveAnalysis(const QString &fp, const QString &summary, const QStringList &tags);
    void applyPresetBypassAnalysis(const QString &fp, const QString &summary, const QStringList &tags);

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
    QSet<QString> m_recursiveWatchPaths;

    // Graph is embedded as Tab 3 (no standalone window)

    QString currentFilePath() const;

    void setupFourColumnLayout();
    void setupContextMenus();

    void bumpWorkspaceEpochAndPurgeStaleAsyncWork();
    void mapsHomeFixAndSetRoot(const QString &dir);
    void refreshWorkspacePickerTitle();
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
    void applyTagSelectionData(const QString &userRoleData);
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

    /// Solid analysis badge only when we have a persisted summary that is non-empty and not an LLM error echo.
    bool pathHasUsableAnalysisSummary(const QString &absPath) const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // MAINWINDOW_H

#include "MainWindow.h"
#include "../core/FileScanner.h"
#include "../core/DocumentParser.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog> // Added
#include <QApplication>
#include <fstream>
#include <algorithm>
#include <set>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    setupToolbar();
    setupLayout();

    // Initialize watcher
    watcher = new QFutureWatcher<std::string>(this);
    connect(watcher, &QFutureWatcher<std::string>::finished, this, &MainWindow::onAnalysisFinished);

    resize(1200, 800);
    setWindowTitle("Smart File Organizer");
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupToolbar()
{
    toolbar = addToolBar("Main Toolbar");
    toolbar->setMovable(false);

    QAction *actOpen = toolbar->addAction("開啟資料夾 (Open Folder)");
    connect(actOpen, &QAction::triggered, this, &MainWindow::openFolder);

    toolbar->addSeparator();

    QAction *actLoadModel = toolbar->addAction("載入模型 (Load Model)");
    actLoadModel->setToolTip("請選擇 ggml-model-*.gguf 檔案");
    connect(actLoadModel, &QAction::triggered, this, &MainWindow::loadModel);

    toolbar->addSeparator();
    
    // Add Checkbox to Toolbar
    chkRecursive = new QCheckBox("包含子資料夾 (Recursive)", this);
    chkRecursive->setChecked(false); // Default to false as per request
    // Connect toggling to re-scan immediately if a folder is already open
    connect(chkRecursive, &QCheckBox::toggled, [this](bool){
        if (!currentPath.isEmpty()) scanFiles();
    });
    toolbar->addWidget(chkRecursive);
}

void MainWindow::setupLayout()
{
    tabWidget = new QTabWidget(this);
    connect(tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    mainLayout->addWidget(tabWidget);

    // === Tab 1: Explorer ===
    explorerTab = new QWidget();
    QVBoxLayout *explorerLayout = new QVBoxLayout(explorerTab);
    
    mainSplitter = new QSplitter(Qt::Horizontal, explorerTab);
    explorerLayout->addWidget(mainSplitter);

    // --- Left Panel (Tags) ---
    leftPanel = new QWidget(this);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->addWidget(new QLabel("🏷️ 標籤庫 (Tags)"));
    
    tagListWidget = new QListWidget(this);
    connect(tagListWidget, &QListWidget::itemClicked, this, &MainWindow::onTagSelected);
    leftLayout->addWidget(tagListWidget);
    
    // Left Panel Actions
    QHBoxLayout *leftActionLayout = new QHBoxLayout();
    btnLeftAddTag = new QPushButton("➕", this);
    btnLeftAddTag->setToolTip("新增標籤 (Add Tag)");
    connect(btnLeftAddTag, &QPushButton::clicked, this, &MainWindow::addTag);
    leftActionLayout->addWidget(btnLeftAddTag);

    btnLeftRemoveTag = new QPushButton("➖", this);
    btnLeftRemoveTag->setToolTip("移除標籤 (Global Delete Tag)");
    connect(btnLeftRemoveTag, &QPushButton::clicked, this, &MainWindow::removeGlobalTag);
    leftActionLayout->addWidget(btnLeftRemoveTag);
    
    leftLayout->addLayout(leftActionLayout);
    
    mainSplitter->addWidget(leftPanel);

    // --- Middle Panel (Files) ---
    middlePanel = new QWidget(this);
    QVBoxLayout *midLayout = new QVBoxLayout(middlePanel);
    midLayout->addWidget(new QLabel("📂 檔案列表 (Files)"));

    txtSearch = new QLineEdit(this);
    txtSearch->setPlaceholderText("搜尋檔案... (Search)");
    connect(txtSearch, &QLineEdit::textChanged, this, &MainWindow::filterFiles);
    midLayout->addWidget(txtSearch);

    fileList = new QListWidget(this);
    connect(fileList, &QListWidget::itemClicked, this, &MainWindow::onFileSelected);
    midLayout->addWidget(fileList);

    mainSplitter->addWidget(middlePanel);

    // --- Right Panel (Details & Preview) ---
    rightPanel = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->addWidget(new QLabel("👁️ 預覽與資訊 (Preview)"));

    // Preview Area
    lblPreviewImage = new QLabel("選擇檔案以預覽 (Select file to preview)", this);
    lblPreviewImage->setAlignment(Qt::AlignCenter);
    lblPreviewImage->setStyleSheet("border: 1px dashed gray; min-height: 200px;");
    rightLayout->addWidget(lblPreviewImage);
    
    txtPreviewText = new QTextEdit(this);
    txtPreviewText->setReadOnly(true);
    txtPreviewText->setVisible(false); // Default hidden
    rightLayout->addWidget(txtPreviewText);

    // Tags Section
    lblTags = new QLabel("標籤: --", this);
    lblTags->setWordWrap(true);
    lblTags->setStyleSheet("font-weight: bold; margin-top: 10px;");
    rightLayout->addWidget(lblTags);

    // Actions
    QHBoxLayout *actionLayout = new QHBoxLayout();
    btnAnalyzeFile = new QPushButton("✨ 分析檔案 (Analyze)", this);
    connect(btnAnalyzeFile, &QPushButton::clicked, this, &MainWindow::analyzeFile);
    actionLayout->addWidget(btnAnalyzeFile);

    btnSaveTags = new QPushButton("💾 儲存標籤 (Save)", this);
    connect(btnSaveTags, &QPushButton::clicked, this, &MainWindow::saveTags);
    btnSaveTags->setEnabled(false);
    actionLayout->addWidget(btnSaveTags);

    // Manual Tag Management
    btnAddTag = new QPushButton("➕ 新增標籤 (Add)", this);
    connect(btnAddTag, &QPushButton::clicked, this, &MainWindow::addTag);
    actionLayout->addWidget(btnAddTag);

    btnRemoveTag = new QPushButton("➖ 移除標籤 (Remove)", this);
    connect(btnRemoveTag, &QPushButton::clicked, this, &MainWindow::removeTag);
    actionLayout->addWidget(btnRemoveTag);

    rightLayout->addLayout(actionLayout);

    lblStatus = new QLabel("狀態: 就緒", this);
    lblStatus->setWordWrap(true);
    rightLayout->addWidget(lblStatus);

    rightLayout->addStretch();
    mainSplitter->addWidget(rightPanel);

    // Set initial sizes: 20% | 40% | 40%
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 2);
    mainSplitter->setStretchFactor(2, 2);

    tabWidget->addTab(explorerTab, "📁 資料夾視圖 (Explorer)");

    // === Tab 2: Graph View ===
    graphWidget = new GraphWidget(&tagManager, this);
    tabWidget->addTab(graphWidget, "🕸️ 關聯視圖 (Graph)");
}

void MainWindow::onTabChanged(int index) {
    if (index == 1) { // Graph Tab
        graphWidget->buildGraph();
    }
}

void MainWindow::openFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Directory",
                                                    QString(),
                                                    QFileDialog::ShowDirsOnly
                                                    | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        currentPath = dir;
        tagManager.loadTags(currentPath.toStdString());
        scanFiles();
    }
}

void MainWindow::scanFiles()
{
    fileList->clear();
    FileScanner scanner;
    bool recursive = chkRecursive->isChecked();
    
    std::vector<std::string> files = scanner.scanDirectory(currentPath.toStdString(), recursive);

    for (const auto& file : files) {
        QString qfilePath = QString::fromStdString(file);
        QFileInfo fileInfo(qfilePath);
        QString fileName = fileInfo.fileName();

        QListWidgetItem* item = new QListWidgetItem(fileName, fileList);
        item->setData(Qt::UserRole, qfilePath); 

        QStringList fastTags = getFastPathTags(fileName);
        if (!fastTags.isEmpty()) {
            for (const QString& tag : fastTags) {
                tagManager.addTag(qfilePath, tag, false);
            }
        }
    }
    
    updateTagList(); 
    lblStatus->setText(QString("目前資料夾: %1 (找到 %2 個檔案)").arg(currentPath).arg(files.size()));
}
void MainWindow::updateTagList()
{
    tagListWidget->clear();
    std::vector<std::string> tags = tagManager.getAllTags();
    
    // Always add an "All Files" option
    QListWidgetItem* allItem = new QListWidgetItem("All Files");
    allItem->setData(Qt::UserRole, "ALL");
    tagListWidget->addItem(allItem);

    for (const auto& tag : tags) {
        tagListWidget->addItem(QString::fromStdString(tag));
    }
}

void MainWindow::onTagSelected(QListWidgetItem *item)
{
    QString tag = item->text();
    QString data = item->data(Qt::UserRole).toString();
    
    if (data == "ALL") {
        // Show all files
        for(int i=0; i<fileList->count(); ++i) {
            fileList->item(i)->setHidden(false);
        }
    } else {
        // Filter by tag
        std::vector<std::string> filesWithTag = tagManager.getFilesByTag(tag.toStdString());
        std::set<std::string> fileSet(filesWithTag.begin(), filesWithTag.end());
        
        for(int i=0; i<fileList->count(); ++i) {
            QListWidgetItem *fItem = fileList->item(i);
            QString fname = fItem->text(); 
            
            // Check if this file is in the tag set
            std::filesystem::path p(fname.toStdString());
            std::string filenameOnly = p.filename().string();
            
            fItem->setHidden(fileSet.find(filenameOnly) == fileSet.end());
        }
    }
}

void MainWindow::loadModel()
{
    QString fileName = QFileDialog::getOpenFileName(this, "載入模型 (Load Model)",
                                                    QString(),
                                                    "GGUF Models (*.gguf);;All Files (*)");

    if (!fileName.isEmpty()) {
        lblStatus->setText("正在載入模型... (Loading Model...)");
        QApplication::processEvents(); // Force update UI

        if (llamaEngine.loadModel(fileName.toStdString())) {
            lblStatus->setText("模型載入成功! (Model loaded!)");
            QMessageBox::information(this, "Success", "模型載入成功！");
        } else {
            lblStatus->setText("模型載入失敗 (Failed to load model)");
            QMessageBox::critical(this, "Error", "模型載入失敗 (Failed to load model)");
        }
    }
}

void MainWindow::analyzeFile()
{
    QList<QListWidgetItem*> selectedItems = fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select a file first.");
        return;
    }

    QString filename = selectedItems.first()->text();
    std::filesystem::path path(currentPath.toStdString());
    path /= filename.toStdString();
    QString filePath = QString::fromStdString(path.string());
    
    std::string content = "";
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    static const std::set<std::string> textExts = {
        ".txt", ".md", ".log", ".tex", ".rtf",
        ".cpp", ".h", ".c", ".hpp", ".cs", ".java", ".py", ".js", ".ts", 
        ".html", ".css", ".json", ".xml", ".yaml", ".yml", ".ini", ".conf", ".env",
        ".bat", ".sh", ".ps1", ".go", ".rs", ".lua", ".sql", ".php"
    };

    if (textExts.count(ext)) {
        try {
            std::ifstream f(filePath.toStdString());
            if (f.is_open()) {
                char buffer[1025];
                f.read(buffer, 1024);
                buffer[f.gcount()] = '\0';
                content = std::string(buffer);
                lblStatus->setText("正在分析檔案內容... (這可能需要幾秒鐘)");
            }
        } catch (...) {}
    } 
    else if (ext == ".docx" || ext == ".xlsx" || ext == ".pdf") {
        lblStatus->setText(QString("正在解析文件內容: %1").arg(filename));
        content = DocumentParser::extractText(filePath.toStdString());
        if (content.length() > 2000) content = content.substr(0, 2000) + "...";
    }
    else {
        lblStatus->setText("正在分析檔名...");
    }

    btnAnalyzeFile->setEnabled(false);
    btnSaveTags->setEnabled(false);
    fileList->setEnabled(false);
    
    QFuture<std::string> future = QtConcurrent::run([this, filename, content]() {
        return llamaEngine.suggestTags(filename.toStdString(), content);
    });

    watcher->setFuture(future);
}

void MainWindow::onAnalysisFinished()
{
    std::string result = watcher->result();
    
    // Re-enable UI
    btnAnalyzeFile->setEnabled(true);
    fileList->setEnabled(true);

    if (result.rfind("Error:", 0) == 0) { // Starts with "Error:"
        lblStatus->setText("分析失敗 (Analysis Failed)");
        QMessageBox::critical(this, "Analysis Error", QString::fromStdString(result));
        return;
    }

    lblStatus->setText("分析完成 (Analysis complete)");
    
    // Auto-save tags
    btnSaveTags->setProperty("pendingTags", QString::fromStdString(result));
    saveTags(); // Automatically save
    
    // Update UI to show success
    lblTags->setText("標籤: " + QString::fromStdString(result));
    QMessageBox::information(this, "Analysis Finished", "分析完成並已自動儲存標籤！\n(Analysis complete and tags saved!)");
}

void MainWindow::onFileSelected(QListWidgetItem *item)
{
    QString filePathStr = item->text();
    // Resolve full path if item text is relative
    std::filesystem::path p(currentPath.toStdString());
    p /= filePathStr.toStdString();
    
    updateTagDisplay(QString::fromStdString(p.filename().string()));
    updateFilePreview(QString::fromStdString(p.string()));
    btnSaveTags->setEnabled(false);
}

void MainWindow::updateFilePreview(const QString& filePath)
{
    std::filesystem::path p(filePath.toStdString());
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    // Hide all first
    lblPreviewImage->setVisible(false);
    txtPreviewText->setVisible(false);

    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp") {
        QPixmap pixmap(filePath);
        if (!pixmap.isNull()) {
            lblPreviewImage->setPixmap(pixmap.scaled(lblPreviewImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            lblPreviewImage->setVisible(true);
        } else {
            lblPreviewImage->setText("無法載入圖片 (Image Load Failed)");
            lblPreviewImage->setVisible(true);
        }
    } else if (ext == ".docx" || ext == ".xlsx" || ext == ".pdf") {
        txtPreviewText->setVisible(true);
        std::string content = DocumentParser::extractText(filePath.toStdString());
        if (content.empty()) content = "(No searchable text found or encrypted)";
        txtPreviewText->setText(QString::fromStdString(content));
    } else {
        // Text preview
        txtPreviewText->setVisible(true);
        std::ifstream f(filePath.toStdString());
        if (f.is_open()) {
             char buffer[2048];
             f.read(buffer, 2047);
             buffer[f.gcount()] = '\0';
             txtPreviewText->setText(QString::fromUtf8(buffer));
        } else {
             txtPreviewText->setText("(無法讀取檔案內容)");
        }
    }
}

void MainWindow::updateTagDisplay(const QString& filePath)
{
    std::filesystem::path path(filePath.toStdString());
    std::string filename = path.filename().string();
    std::vector<std::string> tags = tagManager.getTags(filename);
    
    QString tagStr = "標籤: ";
    if (tags.empty()) {
        tagStr += "(無)";
    } else {
        for (size_t i = 0; i < tags.size(); ++i) {
            tagStr += QString::fromStdString(tags[i]);
            if (i < tags.size() - 1) tagStr += ", ";
        }
    }
    lblTags->setText(tagStr);
}

void MainWindow::saveTags()
{
    QList<QListWidgetItem*> selectedItems = fileList->selectedItems();
    if (selectedItems.isEmpty()) return;

    QString filePath = selectedItems.first()->text();
    std::filesystem::path path(filePath.toStdString());
    std::string filename = path.filename().string();
    
    QString pendingTags = btnSaveTags->property("pendingTags").toString();
    if (pendingTags.isEmpty()) return;
    
    // Simple parsing of comma-separated tags
    QStringList tagList = pendingTags.split(',', Qt::SkipEmptyParts);
    std::vector<std::string> newTags;
    for (const QString& t : tagList) {
        newTags.push_back(t.trimmed().toStdString());
    }
    
    tagManager.setTags(filename, newTags);
    updateTagDisplay(filePath);
    updateTagList(); // Refresh left panel to show new tags immediately
    
    lblStatus->setText("標籤已儲存 (Tags saved)");
    btnSaveTags->setEnabled(false);
}

void MainWindow::filterFiles(const QString &text)
{
    QString query = text.trimmed().toLower();
    
    for (int i = 0; i < fileList->count(); ++i) {
        QListWidgetItem *item = fileList->item(i);
        QString filePath = item->text();
        std::filesystem::path path(filePath.toStdString());
        QString filename = QString::fromStdString(path.filename().string()).toLower();
        
        bool match = false;
        
        // 1. Check filename
        if (filename.contains(query)) {
            match = true;
        } else {
            // 2. Check tags
            std::vector<std::string> tags = tagManager.getTags(path.filename().string());
            for (const auto& tag : tags) {
                if (QString::fromStdString(tag).toLower().contains(query)) {
                    match = true;
                    break;
                }
            }
        }
        
        item->setHidden(!match);
    }
}

void MainWindow::addTag()
{
    QList<QListWidgetItem*> selectedItems = fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select a file first.");
        return;
    }

    QString filename = selectedItems.first()->text();
    std::filesystem::path path(filename.toStdString());
    std::string fnameOnly = path.filename().string();

    QInputDialog dialog(this);
    dialog.setWindowTitle("Add Tag");
    dialog.setLabelText("New Tag Name:");
    dialog.setTextValue("");
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.resize(300, 150); // Optimize size

    if (dialog.exec() == QDialog::Accepted) {
        QString text = dialog.textValue();
        if (!text.isEmpty()) {
            tagManager.addTag(fnameOnly, text.toStdString());
            updateTagDisplay(filename);
            updateTagList(); // Refresh left panel
            lblStatus->setText(QString("已新增標籤: %1").arg(text));
        }
    }
}

void MainWindow::removeTag()
{
    QList<QListWidgetItem*> selectedItems = fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select a file first.");
        return;
    }

    QString filename = selectedItems.first()->text();
    std::filesystem::path path(filename.toStdString());
    std::string fnameOnly = path.filename().string();

    std::vector<std::string> tags = tagManager.getTags(fnameOnly);
    if (tags.empty()) {
        QMessageBox::information(this, "Info", "This file has no tags.");
        return;
    }

    QStringList items;
    for (const auto& t : tags) items << QString::fromStdString(t);

    QInputDialog dialog(this);
    dialog.setWindowTitle("Remove Tag");
    dialog.setLabelText("Select tag to remove:");
    dialog.setComboBoxItems(items);
    dialog.setInputMode(QInputDialog::TextInput); // ComboBox uses TextInput mode with items
    dialog.setComboBoxEditable(false);
    dialog.resize(300, 150); // Optimize size

    if (dialog.exec() == QDialog::Accepted) {
        QString item = dialog.textValue();
        if (!item.isEmpty()) {
            tagManager.removeTag(fnameOnly, item.toStdString());
            updateTagDisplay(filename);
            updateTagList(); // Refresh left panel
            lblStatus->setText(QString("已移除標籤: %1").arg(item));
        }
    }
}


void MainWindow::removeGlobalTag()
{
    QList<QListWidgetItem*> selectedItems = tagListWidget->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::warning(this, "Warning", "Please select a tag from the left list first.");
        return;
    }

    QString tag = selectedItems.first()->text();
    QString data = selectedItems.first()->data(Qt::UserRole).toString();

    if (data == "ALL") {
        QMessageBox::warning(this, "Warning", "Cannot delete 'All Files' category.");
        return;
    }

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Delete Tag", 
                                  QString("Are you sure you want to delete tag '%1' from ALL files?").arg(tag),
                                  QMessageBox::Yes|QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        tagManager.deleteTag(tag.toStdString());
        updateTagList();
        
        // Refresh right panel if a file is selected
        QList<QListWidgetItem*> selectedFiles = fileList->selectedItems();
        if (!selectedFiles.isEmpty()) {
            updateTagDisplay(selectedFiles.first()->text());
        }
        
        lblStatus->setText(QString("已刪除標籤: %1 (Global)").arg(tag));
    }
}

#include "LanguageManager.h"

#include <QHash>
#include <QSettings>

namespace {
static constexpr const char *kSettingsLangKey = "i18n/language";

static LanguageManager::Language fromSettings(const QString &v) {
    const QString s = v.trimmed().toLower();
    if (s == QStringLiteral("en") || s == QStringLiteral("en_us") || s == QStringLiteral("english")) {
        return LanguageManager::Language::EN_US;
    }
    return LanguageManager::Language::ZH_TW;
}

static QString toSettings(LanguageManager::Language lang) {
    return (lang == LanguageManager::Language::EN_US) ? QStringLiteral("en_us") : QStringLiteral("zh_tw");
}
} // namespace

LanguageManager &LanguageManager::instance() {
    static LanguageManager inst;
    return inst;
}

LanguageManager::LanguageManager(QObject *parent) : QObject(parent) {
    QSettings s;
    m_lang = fromSettings(s.value(QString::fromLatin1(kSettingsLangKey)).toString());
}

LanguageManager::Language LanguageManager::language() const {
    return m_lang;
}

void LanguageManager::setLanguage(Language lang) {
    if (m_lang == lang) return;
    m_lang = lang;
    QSettings s;
    s.setValue(QString::fromLatin1(kSettingsLangKey), toSettings(lang));
    emit languageChanged(lang);
}

QString LanguageManager::getText(const QString &key) const {
    // Lightweight dictionary: add keys as needed.
    const bool en = (m_lang == Language::EN_US);

    if (key == QStringLiteral("tab_workspace")) return en ? QStringLiteral("Workspace") : QStringLiteral("核心工作區");
    if (key == QStringLiteral("tab_duplicates")) return en ? QStringLiteral("Duplicate Cleaner") : QStringLiteral("冗餘檔案清理");
    if (key == QStringLiteral("tab_graph")) return en ? QStringLiteral("Graph Analysis") : QStringLiteral("關聯圖譜分析");

    if (key == QStringLiteral("toolbar_open")) return en ? QStringLiteral("Open Folder") : QStringLiteral("開啟資料夾");
    if (key == QStringLiteral("toolbar_duplicates")) return en ? QStringLiteral("Find Duplicates") : QStringLiteral("🧹 尋找冗餘檔案");
    if (key == QStringLiteral("toolbar_settings")) return en ? QStringLiteral("⚙️ Settings") : QStringLiteral("⚙️ 設定 (Settings)");

    if (key == QStringLiteral("btn_analyze")) return en ? QStringLiteral("✨ Analyze") : QStringLiteral("✨ 分析");
    if (key == QStringLiteral("btn_cancel")) return en ? QStringLiteral("⛔ Cancel") : QStringLiteral("⛔ 取消");
    if (key == QStringLiteral("btn_save")) return en ? QStringLiteral("💾 Save") : QStringLiteral("💾 儲存");
    if (key == QStringLiteral("btn_add_tag")) return en ? QStringLiteral("➕ Add Tag") : QStringLiteral("➕ 加入標籤");
    if (key == QStringLiteral("btn_remove_tag")) return en ? QStringLiteral("➖ Remove Tag") : QStringLiteral("➖ 移除標籤");
    if (key == QStringLiteral("btn_add_existing_tag")) return en ? QStringLiteral("🏷️ Add Existing Tag") : QStringLiteral("🏷️ 加入現有標籤");

    if (key == QStringLiteral("btn_physical_archive")) return en ? QStringLiteral("Physical Archive (by Tag)") : QStringLiteral("實體歸檔 (依標籤)");
    if (key == QStringLiteral("btn_undo_archive")) return en ? QStringLiteral("Undo Archive") : QStringLiteral("回上一步 (復原歸檔)");

    if (key == QStringLiteral("btn_scan")) return en ? QStringLiteral("Start Analysis") : QStringLiteral("開始分析");

    // --- Main workspace UI labels (requested) ---
    if (key == QStringLiteral("標籤庫")) return en ? QStringLiteral("Tag Library") : QStringLiteral("標籤庫");
    if (key == QStringLiteral("包含子資料夾")) return en ? QStringLiteral("Include Subfolders") : QStringLiteral("包含子資料夾");
    if (key == QStringLiteral("資料夾樹")) return en ? QStringLiteral("Folder Tree") : QStringLiteral("資料夾樹");
    if (key == QStringLiteral("本機磁碟 (Home)")) return en ? QStringLiteral("Local Disk (Home)") : QStringLiteral("本機磁碟 (Home)");
    if (key == QStringLiteral("檔案清單")) return en ? QStringLiteral("File List") : QStringLiteral("檔案清單");
    if (key == QStringLiteral("預覽與控制")) return en ? QStringLiteral("Preview & Control") : QStringLiteral("預覽與控制");
    if (key == QStringLiteral("選擇檔案以預覽")) return en ? QStringLiteral("Select a file to preview") : QStringLiteral("選擇檔案以預覽");
    if (key == QStringLiteral("標籤管理")) return en ? QStringLiteral("Tag Management") : QStringLiteral("標籤管理");
    if (key == QStringLiteral("檔案操作")) return en ? QStringLiteral("File Operations") : QStringLiteral("檔案操作");
    if (key == QStringLiteral("重新命名")) return en ? QStringLiteral("Rename") : QStringLiteral("重新命名");
    if (key == QStringLiteral("刪除檔案")) return en ? QStringLiteral("Delete File") : QStringLiteral("刪除檔案");
    if (key == QStringLiteral("開啟位置")) return en ? QStringLiteral("Open Location") : QStringLiteral("開啟位置");
    if (key == QStringLiteral("依名稱")) return en ? QStringLiteral("By Name") : QStringLiteral("依名稱");
    if (key == QStringLiteral("搜尋")) return en ? QStringLiteral("Search...") : QStringLiteral("搜尋");

    // Extra sort keys to avoid leftover Chinese in EN
    if (key == QStringLiteral("依日期")) return en ? QStringLiteral("By Date") : QStringLiteral("依日期");
    if (key == QStringLiteral("依大小")) return en ? QStringLiteral("By Size") : QStringLiteral("依大小");

    // Tag filter "All Files"
    if (key == QStringLiteral("All Files")) return en ? QStringLiteral("All Files") : QStringLiteral("所有檔案");

    // --- System / preset tags (presentation layer) ---
    if (key == QStringLiteral("圖片")) return en ? QStringLiteral("Images") : QStringLiteral("圖片");
    if (key == QStringLiteral("🖼️ 圖片")) return en ? QStringLiteral("🖼️ Images") : QStringLiteral("🖼️ 圖片");
    if (key == QStringLiteral("影片")) return en ? QStringLiteral("Videos") : QStringLiteral("影片");
    if (key == QStringLiteral("🎬 影片")) return en ? QStringLiteral("🎬 Videos") : QStringLiteral("🎬 影片");
    if (key == QStringLiteral("音檔")) return en ? QStringLiteral("Audio") : QStringLiteral("音檔");
    if (key == QStringLiteral("🎧 音檔")) return en ? QStringLiteral("🎧 Audio") : QStringLiteral("🎧 音檔");
    if (key == QStringLiteral("音訊")) return en ? QStringLiteral("Audio") : QStringLiteral("音訊");
    if (key == QStringLiteral("🎧 音訊")) return en ? QStringLiteral("🎧 Audio") : QStringLiteral("🎧 音訊");
    if (key == QStringLiteral("文件")) return en ? QStringLiteral("Documents") : QStringLiteral("文件");
    if (key == QStringLiteral("📄 文件")) return en ? QStringLiteral("📄 Documents") : QStringLiteral("📄 文件");
    if (key == QStringLiteral("壓縮檔")) return en ? QStringLiteral("Archives") : QStringLiteral("壓縮檔");
    if (key == QStringLiteral("📦 壓縮檔")) return en ? QStringLiteral("📦 Archives") : QStringLiteral("📦 壓縮檔");
    if (key == QStringLiteral("程式碼")) return en ? QStringLiteral("Source Code") : QStringLiteral("程式碼");
    if (key == QStringLiteral("🧩 程式碼")) return en ? QStringLiteral("🧩 Source Code") : QStringLiteral("🧩 程式碼");
    if (key == QStringLiteral("安裝檔")) return en ? QStringLiteral("Installers") : QStringLiteral("安裝檔");
    if (key == QStringLiteral("📦 安裝檔")) return en ? QStringLiteral("📦 Installers") : QStringLiteral("📦 安裝檔");
    if (key == QStringLiteral("備份檔")) return en ? QStringLiteral("Backups") : QStringLiteral("備份檔");
    if (key == QStringLiteral("🗄️ 備份檔")) return en ? QStringLiteral("🗄️ Backups") : QStringLiteral("🗄️ 備份檔");
    if (key == QStringLiteral("設定")) return en ? QStringLiteral("Settings") : QStringLiteral("設定");
    if (key == QStringLiteral("⚙️ 設定")) return en ? QStringLiteral("⚙️ Settings") : QStringLiteral("⚙️ 設定");
    if (key == QStringLiteral("設計")) return en ? QStringLiteral("Design") : QStringLiteral("設計");
    if (key == QStringLiteral("資料庫")) return en ? QStringLiteral("Databases") : QStringLiteral("資料庫");
    if (key == QStringLiteral("🗃️ 資料庫")) return en ? QStringLiteral("🗃️ Databases") : QStringLiteral("🗃️ 資料庫");
    if (key == QStringLiteral("學校作業")) return en ? QStringLiteral("Schoolwork") : QStringLiteral("學校作業");
    if (key == QStringLiteral("應用程式")) return en ? QStringLiteral("Applications") : QStringLiteral("應用程式");
    if (key == QStringLiteral("履歷")) return en ? QStringLiteral("Resume") : QStringLiteral("履歷");
    if (key == QStringLiteral("專案")) return en ? QStringLiteral("Projects") : QStringLiteral("專案");
    if (key == QStringLiteral("🧩 專案")) return en ? QStringLiteral("🧩 Projects") : QStringLiteral("🧩 專案");

    // Preview tag display group labels / status snippets
    if (key == QStringLiteral("個人標籤")) return en ? QStringLiteral("Personal Tags") : QStringLiteral("個人標籤");
    if (key == QStringLiteral("AI 智能建議")) return en ? QStringLiteral("AI Suggestions") : QStringLiteral("AI 智能建議");
    if (key == QStringLiteral("AI 智慧摘要")) return en ? QStringLiteral("AI Smart Summary") : QStringLiteral("AI 智慧摘要");
    if (key == QStringLiteral("尚未分析")) return en ? QStringLiteral("Not analyzed yet") : QStringLiteral("尚未分析");
    if (key == QStringLiteral("無")) return en ? QStringLiteral("None") : QStringLiteral("無");
    if (key == QStringLiteral("分析完成")) return en ? QStringLiteral("Analysis complete") : QStringLiteral("分析完成");
    if (key == QStringLiteral("分析中…")) return en ? QStringLiteral("Analyzing…") : QStringLiteral("分析中…");
    if (key == QStringLiteral("取消中…")) return en ? QStringLiteral("Canceling…") : QStringLiteral("取消中…");
    if (key == QStringLiteral("已取消")) return en ? QStringLiteral("Canceled") : QStringLiteral("已取消");
    if (key == QStringLiteral("分析失敗")) return en ? QStringLiteral("Analysis failed") : QStringLiteral("分析失敗");
    if (key == QStringLiteral("等待模型載入完成…")) return en ? QStringLiteral("Waiting for model to finish loading…") : QStringLiteral("等待模型載入完成…");
    if (key == QStringLiteral("此項目不可分析")) return en ? QStringLiteral("This item cannot be analyzed") : QStringLiteral("此項目不可分析");
    if (key == QStringLiteral("分析完成（無選取檔案）")) return en ? QStringLiteral("Analysis complete (no file selected)") : QStringLiteral("分析完成（無選取檔案）");

    // --- More dynamic UI strings (workplace / duplicates / graph) ---
    if (key == QStringLiteral("標籤")) return en ? QStringLiteral("Tags") : QStringLiteral("標籤");
    if (key == QStringLiteral("狀態")) return en ? QStringLiteral("Status") : QStringLiteral("狀態");
    if (key == QStringLiteral("資料夾")) return en ? QStringLiteral("Folder") : QStringLiteral("資料夾");
    if (key == QStringLiteral("檔案數")) return en ? QStringLiteral("Files") : QStringLiteral("檔案數");
    if (key == QStringLiteral("僅此層")) return en ? QStringLiteral("This level only") : QStringLiteral("僅此層");
    if (key == QStringLiteral("虛擬標籤檢視")) return en ? QStringLiteral("Virtual Tag View") : QStringLiteral("虛擬標籤檢視");

    if (key == QStringLiteral("目標目錄")) return en ? QStringLiteral("Target Folder") : QStringLiteral("目標目錄");
    if (key == QStringLiteral("瀏覽...")) return en ? QStringLiteral("Browse...") : QStringLiteral("瀏覽...");
    if (key == QStringLiteral("開始分析")) return en ? QStringLiteral("Start Analysis") : QStringLiteral("開始分析");
    if (key == QStringLiteral("停止掃描")) return en ? QStringLiteral("Stop Scan") : QStringLiteral("停止掃描");
    if (key == QStringLiteral("將勾選檔案移至待處理區")) return en ? QStringLiteral("Move Selected to Staging") : QStringLiteral("將勾選檔案移至待處理區");
    if (key == QStringLiteral("重複檔案群組 (依 Hash)")) return en ? QStringLiteral("Duplicate Groups (by Hash)") : QStringLiteral("重複檔案群組 (依 Hash)");
    if (key == QStringLiteral("檔案路徑")) return en ? QStringLiteral("File Path") : QStringLiteral("檔案路徑");

    if (key == QStringLiteral("標籤過濾")) return en ? QStringLiteral("Tag Filter") : QStringLiteral("標籤過濾");
    if (key == QStringLiteral("顯示全部")) return en ? QStringLiteral("Show All") : QStringLiteral("顯示全部");
    if (key == QStringLiteral("關聯圖譜")) return en ? QStringLiteral("Graph") : QStringLiteral("關聯圖譜");
    if (key == QStringLiteral("當前目錄包含過多檔案")) return en ? QStringLiteral("The current folder contains too many files") : QStringLiteral("當前目錄包含過多檔案");
    if (key == QStringLiteral("狀態：就緒（尚未開始分析）")) return en ? QStringLiteral("Status: Ready (not started)") : QStringLiteral("狀態：就緒（尚未開始分析）");
    if (key == QStringLiteral("僅此層")) return en ? QStringLiteral("This level only") : QStringLiteral("僅此層");
    if (key == QStringLiteral("遞迴")) return en ? QStringLiteral("Recursive") : QStringLiteral("遞迴");
    if (key == QStringLiteral("就緒")) return en ? QStringLiteral("Ready") : QStringLiteral("就緒");
    if (key == QStringLiteral("背景全域掃描完成")) return en ? QStringLiteral("Background scan complete") : QStringLiteral("背景全域掃描完成");
    if (key == QStringLiteral("狀態：請先選擇目標資料夾")) return en ? QStringLiteral("Status: Please choose a target folder first") : QStringLiteral("狀態：請先選擇目標資料夾");
    if (key == QStringLiteral("狀態：停止中…")) return en ? QStringLiteral("Status: Stopping…") : QStringLiteral("狀態：停止中…");
    if (key == QStringLiteral("狀態：掃描檔案大小分群中…")) return en ? QStringLiteral("Status: Grouping files by size…") : QStringLiteral("狀態：掃描檔案大小分群中…");
    if (key == QStringLiteral("狀態：已取消")) return en ? QStringLiteral("Status: Cancelled") : QStringLiteral("狀態：已取消");
    if (key == QStringLiteral("狀態：未找到重複檔案")) return en ? QStringLiteral("Status: No duplicates found") : QStringLiteral("狀態：未找到重複檔案");
    if (key == QStringLiteral("狀態：掃描完成，請勾選要移動的檔案")) return en ? QStringLiteral("Status: Scan complete. Select files to move.") : QStringLiteral("狀態：掃描完成，請勾選要移動的檔案");
    if (key == QStringLiteral("狀態：正在計算 SHA-256…")) return en ? QStringLiteral("Status: Calculating SHA-256…") : QStringLiteral("狀態：正在計算 SHA-256…");
    if (key == QStringLiteral("狀態：已搬移 %1 個檔案到待處理區")) return en ? QStringLiteral("Status: Moved %1 files to staging") : QStringLiteral("狀態：已搬移 %1 個檔案到待處理區");
    if (key == QStringLiteral("請選擇要掃描的資料夾")) return en ? QStringLiteral("Choose a folder to scan") : QStringLiteral("請選擇要掃描的資料夾");

    if (key == QStringLiteral("正在自動載入模型… %1")) return en ? QStringLiteral("Auto-loading model… %1") : QStringLiteral("正在自動載入模型… %1");
    if (key == QStringLiteral("正在載入新模型… %1")) return en ? QStringLiteral("Loading new model… %1") : QStringLiteral("正在載入新模型… %1");
    if (key == QStringLiteral("模型已自動載入 (Model auto-loaded)")) return en ? QStringLiteral("Model auto-loaded") : QStringLiteral("模型已自動載入 (Model auto-loaded)");
    if (key == QStringLiteral("模型自動載入失敗 (Auto-load failed)")) return en ? QStringLiteral("Model auto-load failed") : QStringLiteral("模型自動載入失敗 (Auto-load failed)");
    if (key == QStringLiteral("❌ 找不到模型: %1（請確認 assets/models/chat_model.gguf）")) {
        return en ? QStringLiteral("❌ Model not found: %1 (check assets/models/chat_model.gguf)")
                  : QStringLiteral("❌ 找不到模型: %1（請確認 assets/models/chat_model.gguf）");
    }

    if (key == QStringLiteral("載入更多")) return en ? QStringLiteral("Load more") : QStringLiteral("載入更多");
    if (key == QStringLiteral("載入全部")) return en ? QStringLiteral("Load all") : QStringLiteral("載入全部");
    if (key == QStringLiteral("狀態：就緒")) return en ? QStringLiteral("Status: Ready") : QStringLiteral("狀態：就緒");
    if (key == QStringLiteral("已取消掃描")) return en ? QStringLiteral("Scan cancelled") : QStringLiteral("已取消掃描");
    if (key == QStringLiteral("二進位檔：不顯示內容")) return en ? QStringLiteral("Binary file: content not shown") : QStringLiteral("二進位檔：不顯示內容");
    if (key == QStringLiteral("...[內容過長已截斷]")) return en ? QStringLiteral("...[Content truncated]") : QStringLiteral("...[內容過長已截斷]");
    if (key == QStringLiteral("無法提取文字內容（可能為掃描檔或加密）")) {
        return en ? QStringLiteral("Cannot extract text (possibly scanned or encrypted)") : QStringLiteral("無法提取文字內容（可能為掃描檔或加密）");
    }

    if (key == QStringLiteral("資料夾分析")) return en ? QStringLiteral("Folder Analyze") : QStringLiteral("資料夾分析");
    if (key == QStringLiteral("資料夾分析完成")) return en ? QStringLiteral("Folder analysis completed") : QStringLiteral("資料夾分析完成");
    if (key == QStringLiteral("正在資料夾分析")) return en ? QStringLiteral("Folder analyzing") : QStringLiteral("正在資料夾分析");
    if (key == QStringLiteral("在資料夾中顯示")) return en ? QStringLiteral("Reveal in Folder") : QStringLiteral("在資料夾中顯示");
    if (key == QStringLiteral("刪除")) return en ? QStringLiteral("Delete") : QStringLiteral("刪除");
    if (key == QStringLiteral("新的檔名：")) return en ? QStringLiteral("New filename:") : QStringLiteral("新的檔名：");

    if (key == QStringLiteral("預設分類 (System Tags)")) return en ? QStringLiteral("System Tags") : QStringLiteral("預設分類 (System Tags)");
    if (key == QStringLiteral("AI 標籤 (AI Tags)")) return en ? QStringLiteral("AI Tags") : QStringLiteral("AI 標籤 (AI Tags)");

    if (key == QStringLiteral("停止")) return en ? QStringLiteral("Stop") : QStringLiteral("停止");
    if (key == QStringLiteral("已停止資料夾分析")) return en ? QStringLiteral("Folder analysis stopped") : QStringLiteral("已停止資料夾分析");

    if (key == QStringLiteral("合併標籤至...")) return en ? QStringLiteral("Merge Tag into...") : QStringLiteral("合併標籤至...");
    if (key == QStringLiteral("選擇目標標籤:")) return en ? QStringLiteral("Choose target tag:") : QStringLiteral("選擇目標標籤:");
    if (key == QStringLiteral("當前目錄包含過多檔案（共 %1 個），繪製完整關聯圖可能導致畫面雜亂或系統卡頓。是否僅顯示核心標籤與前 50 個關聯檔案？")) {
        return en ? QStringLiteral("The current folder contains too many files (total %1). Rendering the full graph may be cluttered or slow. Show only core tags and the first 50 related files?")
                  : QStringLiteral("當前目錄包含過多檔案（共 %1 個），繪製完整關聯圖可能導致畫面雜亂或系統卡頓。是否僅顯示核心標籤與前 50 個關聯檔案？");
    }

    return key;
}


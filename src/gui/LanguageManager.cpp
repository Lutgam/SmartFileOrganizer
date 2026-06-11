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
    if (key == QStringLiteral("tab_system_log"))
        return en ? QStringLiteral("System Log") : QStringLiteral("系統日誌");
    if (key == QStringLiteral("tab_correction_logs"))
        return en ? QStringLiteral("Correction Logs") : QStringLiteral("AI 糾錯紀錄");
    if (key == QStringLiteral("task_center_subtab_batch"))
        return en ? QStringLiteral("Redundancy Analysis") : QStringLiteral("冗餘檔案分析");
    if (key == QStringLiteral("correction_log_col_select"))
        return en ? QStringLiteral("Select") : QStringLiteral("選取");
    if (key == QStringLiteral("btn_correction_log_select_all"))
        return en ? QStringLiteral("Select all") : QStringLiteral("全部選取");
    if (key == QStringLiteral("btn_correction_log_deselect_all"))
        return en ? QStringLiteral("Deselect all") : QStringLiteral("取消全選");
    if (key == QStringLiteral("btn_delete_selected_correction_logs"))
        return en ? QStringLiteral("🗑️ Delete selected") : QStringLiteral("🗑️ 刪除選取紀錄");
    if (key == QStringLiteral("delete_selected_correction_logs_none"))
        return en ? QStringLiteral("Select at least one correction log entry to delete.")
                  : QStringLiteral("請先勾選要刪除的糾錯紀錄。");
    if (key == QStringLiteral("delete_selected_correction_logs_confirm_title"))
        return en ? QStringLiteral("Delete selected logs") : QStringLiteral("刪除選取紀錄");
    if (key == QStringLiteral("delete_selected_correction_logs_confirm_body")) {
        return en ? QStringLiteral("Delete %1 selected correction log entry(ies)?")
                  : QStringLiteral("確定要刪除 %1 筆已勾選的糾錯紀錄嗎？");
    }
    if (key == QStringLiteral("correction_log_col_timestamp"))
        return en ? QStringLiteral("Timestamp") : QStringLiteral("時間");
    if (key == QStringLiteral("correction_log_col_file"))
        return en ? QStringLiteral("File") : QStringLiteral("檔案名稱");
    if (key == QStringLiteral("correction_log_col_tag"))
        return en ? QStringLiteral("Rejected Tag") : QStringLiteral("誤判標籤");
    if (key == QStringLiteral("btn_clear_correction_logs"))
        return en ? QStringLiteral("🗑️ Clear correction logs") : QStringLiteral("🗑️ 清空糾錯紀錄");
    if (key == QStringLiteral("clear_correction_logs_confirm_title"))
        return en ? QStringLiteral("Clear correction logs") : QStringLiteral("清空糾錯紀錄");
    if (key == QStringLiteral("clear_correction_logs_confirm_body")) {
        return en ? QStringLiteral("Remove all RLHF correction entries from this workspace? This cannot be undone.")
                  : QStringLiteral("確定要清空此工作區的所有 AI 糾錯紀錄嗎？此操作無法復原。");
    }
    if (key == QStringLiteral("tab_graph")) return en ? QStringLiteral("Graph Analysis") : QStringLiteral("關聯圖譜分析");

    if (key == QStringLiteral("toolbar_open")) return en ? QStringLiteral("Open Folder") : QStringLiteral("開啟資料夾");
    if (key == QStringLiteral("toolbar_duplicates")) return en ? QStringLiteral("Find Duplicates") : QStringLiteral("🧹 尋找冗餘檔案");
    if (key == QStringLiteral("toolbar_settings")) return en ? QStringLiteral("⚙️ Settings") : QStringLiteral("⚙️ 設定 (Settings)");

    if (key == QStringLiteral("btn_analyze")) return en ? QStringLiteral("✨ Analyze") : QStringLiteral("✨ 分析");
    if (key == QStringLiteral("btn_start_ai_analysis"))
        return en ? QStringLiteral("▶️ Global analysis") : QStringLiteral("▶️ 全域分析");
    if (key == QStringLiteral("btn_stop_analysis"))
        return en ? QStringLiteral("⏸️ Stop analysis") : QStringLiteral("⏸️ 停止分析");
    if (key == QStringLiteral("btn_cancel")) return en ? QStringLiteral("⛔ Cancel") : QStringLiteral("⛔ 取消");
    if (key == QStringLiteral("btn_save")) return en ? QStringLiteral("💾 Save") : QStringLiteral("💾 儲存");
    if (key == QStringLiteral("btn_add_tag")) return en ? QStringLiteral("➕ Add Tag") : QStringLiteral("➕ 加入標籤");
    if (key == QStringLiteral("btn_remove_tag")) return en ? QStringLiteral("➖ Remove Tag") : QStringLiteral("➖ 移除標籤");
    if (key == QStringLiteral("btn_add_existing_tag")) return en ? QStringLiteral("🏷️ Add Existing Tag") : QStringLiteral("🏷️ 加入現有標籤");
    if (key == QStringLiteral("btn_rlhf_tag_correct"))
        return en ? QStringLiteral("AI Tag Correction") : QStringLiteral("AI 標籤糾錯");
    if (key == QStringLiteral("rlhf_tag_dialog_title"))
        return en ? QStringLiteral("AI Tag Correction") : QStringLiteral("AI 標籤糾錯");
    if (key == QStringLiteral("rlhf_tag_error_title"))
        return en ? QStringLiteral("Action Error") : QStringLiteral("操作錯誤");
    if (key == QStringLiteral("rlhf_tag_select_file_first"))
        return en ? QStringLiteral("Select a file in the list to correct its tags.")
                  : QStringLiteral("請先在列表中選擇要糾錯的檔案。");
    if (key == QStringLiteral("rlhf_tag_info_title"))
        return en ? QStringLiteral("Notice") : QStringLiteral("提示");
    if (key == QStringLiteral("rlhf_tag_no_tags_on_file"))
        return en ? QStringLiteral("This file has no tags to correct.")
                  : QStringLiteral("此檔案目前沒有任何標籤，無需糾錯。");
    if (key == QStringLiteral("rlhf_tag_pick_prompt")) {
        return en ? QStringLiteral("File: %1\nSelect the incorrect tag:")
                  : QStringLiteral("檔案: %1\n請選擇判斷錯誤的標籤：");
    }
    if (key == QStringLiteral("rlhf_tag_log_contextual")) {
        return en ? QStringLiteral("[RLHF] Tag '%2' on '%1' marked as mislabeled; saved to local weight reference.")
                  : QStringLiteral("[RLHF] 檔案 '%1' 的標籤 '%2' 已標記為誤判，寫入本地權重參考檔。");
    }

    if (key == QStringLiteral("btn_physical_archive")) return en ? QStringLiteral("Physical Archive (by Tag)") : QStringLiteral("實體歸檔 (依標籤)");
    if (key == QStringLiteral("btn_undo_archive")) return en ? QStringLiteral("Undo Archive") : QStringLiteral("回上一步 (復原歸檔)");
    if (key == QStringLiteral("physical_archive_ui_warning")) {
        return en ? QStringLiteral("Warning: physical archive moves files on disk. Only the last run can be undone via \"Undo Archive\".")
                  : QStringLiteral("警告：實體歸檔會改變磁碟上的實體檔案位置；僅能透過「回上一步 (復原歸檔)」復原上一輪操作。");
    }
    if (key == QStringLiteral("physical_archive_confirm_title"))
        return en ? QStringLiteral("Physical archive — destructive operation") : QStringLiteral("實體歸檔 — 破壞性操作");
    if (key == QStringLiteral("physical_archive_confirm_body")) {
        return en ? QStringLiteral("You are about to move files under:\n%1\nFiles will be placed into subfolders named after their primary tag. This changes real paths on disk.\n\nContinue?")
                  : QStringLiteral("即將對以下工作區執行實體歸檔：\n【%1】\n檔案將依主要標籤被移入對應子資料夾；此操作會改變實體檔案位置。\n\n確定要繼續嗎？");
    }
    if (key == QStringLiteral("physical_archive_need_workspace"))
        return en ? QStringLiteral("Open a workspace folder first.") : QStringLiteral("請先開啟工作區資料夾。");
    if (key == QStringLiteral("physical_archive_high_risk")) {
        return en ? QStringLiteral("For safety, physical archive is not allowed on system root, user home, or Desktop. Choose a dedicated project folder.")
                  : QStringLiteral("為保護系統安全，禁止對系統核心、使用者根目錄或桌面執行全域實體歸檔；請指定專用工作資料夾。");
    }
    if (key == QStringLiteral("physical_archive_no_moves")) {
        return en ? QStringLiteral("No tagged files are ready to archive in this workspace.")
                  : QStringLiteral("目前工作區沒有可歸檔的已標記檔案。");
    }

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
    if (key == QStringLiteral("摘要")) return en ? QStringLiteral("Summary") : QStringLiteral("摘要");
    if (key == QStringLiteral("標籤建議")) return en ? QStringLiteral("Tag Suggestions") : QStringLiteral("標籤建議");
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

    if (key == QStringLiteral("btn_batch_bg_running"))
        return en ? QStringLiteral("🔄 Background analysis running…") : QStringLiteral("🔄 背景分析運作中…");

    if (key == QStringLiteral("ui_target_analyzing"))
        return en ? QStringLiteral("⚙ Analyzing: %1") : QStringLiteral("⚙ 分析中: %1");
    if (key == QStringLiteral("ui_target_next_line"))
        return en ? QStringLiteral("⏭ Next in line: %1") : QStringLiteral("⏭ 下一個: %1");
    if (key == QStringLiteral("ui_target_lock_folder"))
        return en ? QStringLiteral("🎯 Priority folder: %1") : QStringLiteral("🎯 優先資料夾: %1");
    if (key == QStringLiteral("ui_target_lock_file"))
        return en ? QStringLiteral("🎯 Priority target: %1") : QStringLiteral("🎯 鎖定分析: %1");

    if (key == QStringLiteral("graph_max_nodes")) return en ? QStringLiteral("Max files") : QStringLiteral("檔案節點上限");

    if (key == QStringLiteral("資料夾分析")) return en ? QStringLiteral("Folder Analyze") : QStringLiteral("資料夾分析");
    if (key == QStringLiteral("資料夾分析完成")) return en ? QStringLiteral("Folder analysis completed") : QStringLiteral("資料夾分析完成");
    if (key == QStringLiteral("正在資料夾分析")) return en ? QStringLiteral("Folder analyzing") : QStringLiteral("正在資料夾分析");
    if (key == QStringLiteral("在資料夾中顯示")) return en ? QStringLiteral("Reveal in Folder") : QStringLiteral("在資料夾中顯示");
    if (key == QStringLiteral("刪除")) return en ? QStringLiteral("Delete") : QStringLiteral("刪除");
    if (key == QStringLiteral("新的檔名：")) return en ? QStringLiteral("New filename:") : QStringLiteral("新的檔名：");

    if (key == QStringLiteral("副檔名分類")) return en ? QStringLiteral("Extension Classification") : QStringLiteral("副檔名分類");
    if (key == QStringLiteral("預設標籤分類 (18大類)")) {
        return en ? QStringLiteral("Default Categories (18)") : QStringLiteral("預設標籤分類 (18大類)");
    }

    if (key == QStringLiteral("預設分類 (System Tags)")) return en ? QStringLiteral("System Tags") : QStringLiteral("預設分類 (System Tags)");
    if (key == QStringLiteral("AI 標籤 (AI Tags)")) return en ? QStringLiteral("AI Tags") : QStringLiteral("AI 標籤 (AI Tags)");

    if (key == QStringLiteral("停止")) return en ? QStringLiteral("Stop") : QStringLiteral("停止");
    if (key == QStringLiteral("已停止資料夾分析")) return en ? QStringLiteral("Folder analysis stopped") : QStringLiteral("已停止資料夾分析");

    if (key == QStringLiteral("合併標籤至...")) return en ? QStringLiteral("Merge Tag into...") : QStringLiteral("合併標籤至...");
    if (key == QStringLiteral("選擇目標標籤:")) return en ? QStringLiteral("Choose target tag:") : QStringLiteral("選擇目標標籤:");

    if (key == QStringLiteral("AI 自動收斂標籤")) return en ? QStringLiteral("AI Auto-Consolidate Tags") : QStringLiteral("AI 自動收斂標籤");
    if (key == QStringLiteral("AI 思考中…")) return en ? QStringLiteral("AI thinking…") : QStringLiteral("AI 思考中…");
    if (key == QStringLiteral("已自動合併 %1 組標籤")) return en ? QStringLiteral("Auto-merged %1 tag groups") : QStringLiteral("已自動合併 %1 組標籤");
    if (key == QStringLiteral("folder_report_title")) return en ? QStringLiteral("Folder analysis report") : QStringLiteral("資料夾分析報告");
    if (key == QStringLiteral("folder_report_body")) {
        return en ? QStringLiteral("Completed analysis for %1 file(s).\nGenerated %2 AI tag assignment(s).\nFound %3 redundant file(s) (identical content).")
                    : QStringLiteral("已完成 %1 個檔案分析。\n生成了 %2 個 AI 標籤。\n發現 %3 個冗餘檔案（內容完全重複）。");
    }
    if (key == QStringLiteral("分析完成（重複內容：已套用快取）")) {
        return en ? QStringLiteral("Done (duplicate content — applied cached result)") : QStringLiteral("分析完成（重複內容：已套用快取）");
    }

    if (key == QStringLiteral("redundancy_dialog_title")) return en ? QStringLiteral("Redundant files report") : QStringLiteral("冗餘檔案報告");
    if (key == QStringLiteral("redundancy_dialog_body")) {
        return en ? QStringLiteral("Analysis finished.\nCompleted %1 file(s).\n%2 new tag assignment(s). Found %3 redundant file(s) (identical content).")
                    : QStringLiteral("分析完成。\n已完成 %1 個檔案分析。\n共新增 %2 個標籤，發現 %3 個冗餘檔案（內容完全重複）。");
    }
    if (key == QStringLiteral("redundancy_delete_checked"))
        return en ? QStringLiteral("Delete checked redundant files") : QStringLiteral("刪除勾選的冗餘檔案");
    if (key == QStringLiteral("redundancy_dialog_body_grouped")) {
        return en ? QStringLiteral("Analysis finished.\nCompleted %1 file(s). %2 new tag assignment(s).\nSame-content duplicates: %3 path(s) to review. Same-name / different-content: %4 path(s) to review.")
                    : QStringLiteral("分析完成。\n已完成 %1 個檔案分析，共新增 %2 個標籤。\n內容相同（Hash）可檢視路徑：%3；檔名相同但內容不同：%4。");
    }
    if (key == QStringLiteral("redundancy_section_hash"))
        return en ? QStringLiteral("Same content (SHA-256)") : QStringLiteral("內容相同（SHA-256）");
    if (key == QStringLiteral("redundancy_section_name")) {
        return en ? QStringLiteral("Same filename, different content (version conflict)") : QStringLiteral("檔名相同、內容不同（版本衝突）");
    }
    if (key == QStringLiteral("redundancy_group_hash_title")) {
        return en ? QStringLiteral("📁 Same-content group (Hash: %1) — %2 file(s)")
                    : QStringLiteral("📁 內容相同群組 (Hash: %1) — 共 %2 個檔案");
    }
    if (key == QStringLiteral("redundancy_group_name_title")) {
        return en ? QStringLiteral("📄 Filename: \"%1\" — %2 path(s)")
                    : QStringLiteral("📄 檔名：「%1」— 共 %2 個路徑");
    }
    if (key == QStringLiteral("redundancy_execute_delete"))
        return en ? QStringLiteral("Execute delete") : QStringLiteral("執行刪除");
    if (key == QStringLiteral("bg_analyze_running")) return en ? QStringLiteral("🔄 Background analysis… (%1)") : QStringLiteral("🔄 背景分析中… (%1)");
    if (key == QStringLiteral("bg_analyze_queue"))
        return en ? QStringLiteral("🔄 Analyzing… (%1 file(s) remaining)") : QStringLiteral("🔄 分析中… (剩餘 %1 個檔案)");
    if (key == QStringLiteral("bg_analyze_queue_dir"))
        return en ? QStringLiteral("🔄 Analyzing… [folder: %1] (%2 remaining)")
                  : QStringLiteral("🔄 分析中… [目錄: %1] (剩餘 %2)");
    if (key == QStringLiteral("tab_task_center"))
        return en ? QStringLiteral("Task Center") : QStringLiteral("任務控制中心");
    if (key == QStringLiteral("task_center_clean_selected"))
        return en ? QStringLiteral("Delete checked files") : QStringLiteral("清理勾選檔案");
    if (key == QStringLiteral("tag_group_under"))
        return en ? QStringLiteral("Group under…") : QStringLiteral("收納至…");
    if (key == QStringLiteral("tag_pick_parent"))
        return en ? QStringLiteral("Pick parent tag:") : QStringLiteral("選擇父標籤：");
    if (key == QStringLiteral("tag_group_under_none"))
        return en ? QStringLiteral("No other AI tag can be a parent.") : QStringLiteral("沒有可作為父節點的其他 AI 標籤。");
    if (key == QStringLiteral("tag_group_under_invalid"))
        return en ? QStringLiteral("Cannot set parent (cycle or invalid).") : QStringLiteral("無法設定父節點（可能造成循環或無效）。");
    if (key == QStringLiteral("tag_delete_parent_has_children")) {
        return en ? QStringLiteral("Tag \"%1\" has child tags in the library. Dissolve group (children move to top level) or delete this tag and all descendants?")
                  : QStringLiteral("標籤「%1」在標籤庫中仍有子標籤。要解散群組（子標籤移至頂層），或連同子樹一併刪除？");
    }
    if (key == QStringLiteral("tag_delete_dissolve"))
        return en ? QStringLiteral("Dissolve group") : QStringLiteral("解散群組");
    if (key == QStringLiteral("tag_delete_cascade"))
        return en ? QStringLiteral("Delete with subtree") : QStringLiteral("一併刪除子標籤");
    if (key == QStringLiteral("bg_log_completion")) {
        return en ? QStringLiteral("[%1] Background batch finished: +%2 tag(s), %3 redundant file(s) detected.")
                  : QStringLiteral("[%1] 背景分析完成：新增 %2 個標籤，發現 %3 個冗餘檔案。");
    }
    if (key == QStringLiteral("bg_log_placeholder")) {
        return en ? QStringLiteral("When a background batch finishes, a summary is appended here (no pop-up).")
                  : QStringLiteral("背景批次完成後，摘要將附加於此（不會彈出阻斷視窗）。");
    }
    if (key == QStringLiteral("bg_idle_monitoring"))
        return en ? QStringLiteral("👀 Monitoring & crawler idle…") : QStringLiteral("👀 系統監控與爬蟲待命中…");
    if (key == QStringLiteral("btn_restart_bg_analyze"))
        return en ? QStringLiteral("Resume background analysis") : QStringLiteral("重新開始背景分析");

    if (key == QStringLiteral("bypass_tag_dev_system_file"))
        return en ? QStringLiteral("Development & system files") : QStringLiteral("[開發與系統檔]");
    if (key == QStringLiteral("bypass_summary_dev_dependency")) {
        return en ? QStringLiteral("System-generated development configuration or dependency artifact.")
                  : QStringLiteral("系統產生的開發設定或依賴檔案。");
    }
    if (key == QStringLiteral("bypass_tag_shortcut"))
        return en ? QStringLiteral("System shortcut") : QStringLiteral("系統捷徑");
    if (key == QStringLiteral("bypass_summary_shortcut")) {
        return en ? QStringLiteral("System shortcut pointing to another file or folder.")
                  : QStringLiteral("指向其他檔案或目錄的系統捷徑。");
    }
    if (key == QStringLiteral("bypass_tag_app"))
        return en ? QStringLiteral("Application") : QStringLiteral("應用程式");
    if (key == QStringLiteral("bypass_summary_app")) {
        return en ? QStringLiteral("Executable application or script file.")
                  : QStringLiteral("可執行的應用程式或腳本檔案。");
    }
    if (key == QStringLiteral("bypass_tag_archive"))
        return en ? QStringLiteral("Archive") : QStringLiteral("壓縮檔");
    if (key == QStringLiteral("bypass_summary_archive")) {
        return en ? QStringLiteral("Compressed archive containing multiple files.")
                  : QStringLiteral("包含多個檔案的壓縮封裝檔。");
    }
    if (key == QStringLiteral("settings_system_file_bypass")) {
        return en ? QStringLiteral("Filter system files (skip shortcuts & executables to save compute)")
                  : QStringLiteral("啟用系統檔案過濾（略過捷徑與執行檔以節省算力）");
    }
    if (key == QStringLiteral("settings_language_label"))
        return en ? QStringLiteral("Language") : QStringLiteral("語言");
    if (key == QStringLiteral("settings_lang_zh"))
        return en ? QStringLiteral("Traditional Chinese") : QStringLiteral("繁體中文");
    if (key == QStringLiteral("settings_lang_en"))
        return en ? QStringLiteral("English") : QStringLiteral("English");
    if (key == QStringLiteral("settings_model_label"))
        return en ? QStringLiteral("AI model (.gguf)") : QStringLiteral("AI 模型 (.gguf)");
    if (key == QStringLiteral("settings_model_placeholder"))
        return en ? QStringLiteral("Select a .gguf model file") : QStringLiteral("選擇 .gguf 模型檔");
    if (key == QStringLiteral("settings_bg_auto_analyze")) {
        return en ? QStringLiteral("Enable background auto-analysis (debounced folder watch)")
                  : QStringLiteral("啟用背景自動分析（資料夾變更後延遲觸發）");
    }
    if (key == QStringLiteral("settings_cold_archive_prefix"))
        return en ? QStringLiteral("Ignore & archive files not modified for")
                  : QStringLiteral("忽略並歸檔超過以下時間未修改的檔案");
    if (key == QStringLiteral("settings_cold_off"))
        return en ? QStringLiteral("Off (disabled)") : QStringLiteral("關閉");
    if (key == QStringLiteral("settings_cold_1y"))
        return en ? QStringLiteral("1 year") : QStringLiteral("1 年");
    if (key == QStringLiteral("settings_cold_3y"))
        return en ? QStringLiteral("3 years") : QStringLiteral("3 年");
    if (key == QStringLiteral("settings_cold_5y"))
        return en ? QStringLiteral("5 years") : QStringLiteral("5 年");
    if (key == QStringLiteral("settings_perf_group")) {
        return en ? QStringLiteral("Performance & scheduling") : QStringLiteral("系統效能與排程控制");
    }
    if (key == QStringLiteral("settings_ai_concurrency"))
        return en ? QStringLiteral("AI worker threads") : QStringLiteral("AI 併發執行緒數");
    if (key == QStringLiteral("settings_llama_cpu_threads"))
        return en ? QStringLiteral("LLM CPU threads (llama.cpp)") : QStringLiteral("LLM CPU 執行緒數 (llama.cpp)");
    if (key == QStringLiteral("analysis_nonstandard_format_summary")) {
        return en ? QStringLiteral("AI did not return a standard format")
                  : QStringLiteral("AI 未回傳標準格式");
    }
    if (key == QStringLiteral("analysis_parsed_no_summary")) {
        return en ? QStringLiteral("Parsed successfully but no summary was produced")
                  : QStringLiteral("解析完成但無摘要");
    }
    if (key == QStringLiteral("analysis_manual_classify_tag")) {
        return en ? QStringLiteral("Needs manual classification")
                  : QStringLiteral("需手動分類");
    }
    if (key == QStringLiteral("settings_o1_cache"))
        return en ? QStringLiteral("Enable O(1) metadata cache") : QStringLiteral("啟用 O(1) 快取機制");
    if (key == QStringLiteral("settings_o1_cache_tooltip")) {
        return en ? QStringLiteral("When enabled, reads metadata.json to skip duplicate LLM runs; when disabled, every file is re-analyzed.")
                  : QStringLiteral("勾選時可讀取 metadata.json 快取以略過重複 LLM 分析；取消勾選則每次強制重新分析。");
    }
    if (key == QStringLiteral("redundancy_scan_btn"))
        return en ? QStringLiteral("🔍 Scan duplicates now") : QStringLiteral("🔍 立即掃描重複檔案");
    if (key == QStringLiteral("redundancy_scan_btn_busy"))
        return en ? QStringLiteral("Scanning…") : QStringLiteral("掃描中…");
    if (key == QStringLiteral("redundancy_scan_tooltip")) {
        return en ? QStringLiteral("Standalone whole-workspace scan (size pre-filter, then SHA-256). No AI analysis needed.")
                  : QStringLiteral("獨立掃描整個工作區（先比大小再比 SHA-256），不需要先執行 AI 分析");
    }
    if (key == QStringLiteral("autorules_btn"))
        return en ? QStringLiteral("⚙️ Auto rules") : QStringLiteral("⚙️ 自動規則");
    if (key == QStringLiteral("autorules_tooltip")) {
        return en ? QStringLiteral("Deterministic organize rules (tag or archive by folder/extension/filename). Preview before apply.")
                  : QStringLiteral("建立確定性的整理規則（依資料夾/副檔名/檔名自動標籤或歸檔），套用前可預覽");
    }
    if (key == QStringLiteral("rules_dialog_title"))
        return en ? QStringLiteral("Auto-organize rules") : QStringLiteral("自動整理規則");
    if (key == QStringLiteral("rules_dialog_intro")) {
        return en ? QStringLiteral("Deterministic rules: condition (folder / extension / filename) → action (tag or move). Nothing is changed until you preview and confirm.")
                  : QStringLiteral("確定性規則：條件（資料夾／副檔名／檔名）→ 動作（加標籤或歸檔）。套用前一定會先預覽，確認後才會變更檔案。");
    }
    if (key == QStringLiteral("rules_col_name"))
        return en ? QStringLiteral("Name") : QStringLiteral("規則名稱");
    if (key == QStringLiteral("rules_col_condition"))
        return en ? QStringLiteral("Condition") : QStringLiteral("條件");
    if (key == QStringLiteral("rules_col_action"))
        return en ? QStringLiteral("Action") : QStringLiteral("動作");
    if (key == QStringLiteral("rules_col_param"))
        return en ? QStringLiteral("Tag / Folder") : QStringLiteral("標籤／目標資料夾");
    if (key == QStringLiteral("rules_col_enabled"))
        return en ? QStringLiteral("On") : QStringLiteral("啟用");
    if (key == QStringLiteral("rules_cond_any"))
        return en ? QStringLiteral("(all files)") : QStringLiteral("（所有檔案）");
    if (key == QStringLiteral("rules_action_tag"))
        return en ? QStringLiteral("Add tag") : QStringLiteral("加標籤");
    if (key == QStringLiteral("rules_action_move"))
        return en ? QStringLiteral("Move to folder") : QStringLiteral("歸檔至資料夾");
    if (key == QStringLiteral("rules_btn_add"))
        return en ? QStringLiteral("＋ Add rule") : QStringLiteral("＋ 新增規則");
    if (key == QStringLiteral("rules_btn_remove"))
        return en ? QStringLiteral("Delete") : QStringLiteral("刪除");
    if (key == QStringLiteral("rules_btn_toggle"))
        return en ? QStringLiteral("Enable/Disable") : QStringLiteral("啟用／停用");
    if (key == QStringLiteral("rules_btn_apply"))
        return en ? QStringLiteral("Preview & apply") : QStringLiteral("預覽並套用");
    if (key == QStringLiteral("rules_btn_close"))
        return en ? QStringLiteral("Close") : QStringLiteral("關閉");
    if (key == QStringLiteral("rules_add_title"))
        return en ? QStringLiteral("New rule") : QStringLiteral("新增規則");
    if (key == QStringLiteral("rules_field_folder"))
        return en ? QStringLiteral("Watch folder") : QStringLiteral("監控資料夾");
    if (key == QStringLiteral("rules_folder_placeholder"))
        return en ? QStringLiteral("(empty = whole workspace)") : QStringLiteral("（留空＝整個工作區）");
    if (key == QStringLiteral("rules_field_suffix"))
        return en ? QStringLiteral("Extensions") : QStringLiteral("副檔名");
    if (key == QStringLiteral("rules_field_contains"))
        return en ? QStringLiteral("Filename contains") : QStringLiteral("檔名包含");
    if (key == QStringLiteral("rules_contains_placeholder"))
        return en ? QStringLiteral("e.g. invoice / Screenshot") : QStringLiteral("例如：發票、Screenshot");
    if (key == QStringLiteral("rules_param_placeholder"))
        return en ? QStringLiteral("Tag text, or folder name under the workspace") : QStringLiteral("標籤文字，或工作區下的資料夾名稱");
    if (key == QStringLiteral("rules_add_incomplete"))
        return en ? QStringLiteral("Rule name and tag/folder are required.") : QStringLiteral("規則名稱與「標籤／目標資料夾」為必填。");
    if (key == QStringLiteral("privacy_badge_label"))
        return en ? QStringLiteral("🔒 100% Local AI") : QStringLiteral("🔒 100% 本地運算");
    if (key == QStringLiteral("privacy_badge_tooltip")) {
        return en ? QStringLiteral("All AI inference runs on this machine. Your files are never uploaded.\nThe only network access is the optional one-time model download.")
                  : QStringLiteral("所有 AI 推論皆在本機執行，您的檔案內容永遠不會被上傳。\n唯一的網路行為是（可選的）首次模型下載。");
    }
    if (key == QStringLiteral("model_dl_title"))
        return en ? QStringLiteral("Download AI models") : QStringLiteral("下載 AI 模型");
    if (key == QStringLiteral("model_dl_offer_body")) {
        return en ? QStringLiteral("No AI model found. Download the recommended models now?\n\nThis is the only time the app touches the network — all analysis afterwards runs 100% locally.")
                  : QStringLiteral("找不到 AI 模型。要現在下載建議模型嗎？\n\n這是本程式唯一會連網的一次——下載完成後，所有分析皆 100% 在本機執行。");
    }
    if (key == QStringLiteral("model_dl_intro")) {
        return en ? QStringLiteral("Select the models to download. They are saved next to the app and never leave your machine.")
                  : QStringLiteral("選擇要下載的模型。模型檔將存放在程式目錄旁，您的檔案資料永遠不會離開這台電腦。");
    }
    if (key == QStringLiteral("model_dl_llm_role"))
        return en ? QStringLiteral("file analysis & tagging") : QStringLiteral("檔案分析與標籤生成");
    if (key == QStringLiteral("model_dl_embedding_role"))
        return en ? QStringLiteral("semantic search (vector embeddings)") : QStringLiteral("語意搜尋（向量索引）");
    if (key == QStringLiteral("model_dl_privacy_note")) {
        return en ? QStringLiteral("🔒 Downloading models is the app's only network access. Your files are never uploaded.")
                  : QStringLiteral("🔒 下載模型是本程式唯一的網路行為，您的檔案內容永遠不會被上傳。");
    }
    if (key == QStringLiteral("model_dl_start"))
        return en ? QStringLiteral("Download") : QStringLiteral("開始下載");
    if (key == QStringLiteral("model_dl_cancel"))
        return en ? QStringLiteral("Cancel") : QStringLiteral("取消");
    if (key == QStringLiteral("model_dl_downloading"))
        return en ? QStringLiteral("Downloading %1 …") : QStringLiteral("正在下載 %1 …");
    if (key == QStringLiteral("model_dl_all_done"))
        return en ? QStringLiteral("✅ All models downloaded and verified.") : QStringLiteral("✅ 模型已全部下載並驗證完成。");
    if (key == QStringLiteral("model_dl_failed"))
        return en ? QStringLiteral("❌ Download failed: %1 (check network and retry)") : QStringLiteral("❌ 下載失敗：%1（請檢查網路後重試）");
    if (key == QStringLiteral("model_dl_verify_failed"))
        return en ? QStringLiteral("❌ Verification failed for %1 (incomplete file removed)") : QStringLiteral("❌ %1 驗證失敗（已刪除不完整檔案）");
    if (key == QStringLiteral("tags_relinked_notice")) {
        return en ? QStringLiteral("🔗 Re-attached tags to %1 renamed/moved file(s) by content match")
                  : QStringLiteral("🔗 已透過內容比對，自動將標籤重新接回 %1 個被改名/搬移的檔案");
    }
    if (key == QStringLiteral("archive_recover_title"))
        return en ? QStringLiteral("Unfinished archive detected") : QStringLiteral("偵測到未完成的歸檔作業");
    if (key == QStringLiteral("archive_recover_body")) {
        return en ? QStringLiteral("The app closed during a previous archive run (%1 planned moves). Restore the moved files to their original locations?")
                  : QStringLiteral("上次歸檔作業未正常完成（共 %1 筆搬移計畫）。是否將已搬移的檔案還原回原始位置？");
    }
    if (key == QStringLiteral("archive_recover_done"))
        return en ? QStringLiteral("Archive recovery: restored %1 file(s).")
                  : QStringLiteral("歸檔復原完成：已還原 %1 個檔案。");
    if (key == QStringLiteral("settings_pause_on_battery")) {
        return en ? QStringLiteral("Auto-pause batch analysis on battery power")
                  : QStringLiteral("使用電池供電時自動暫停批次分析");
    }
    if (key == QStringLiteral("batch_battery_pause_notice")) {
        return en ? QStringLiteral("🔋 Running on battery — batch analysis auto-paused (resume anytime)")
                  : QStringLiteral("🔋 偵測到電池供電，批次分析已自動暫停（可隨時按「繼續」恢復）");
    }
    if (key == QStringLiteral("batch_pause_label"))
        return en ? QStringLiteral("Pause") : QStringLiteral("暫停");
    if (key == QStringLiteral("batch_resume_label"))
        return en ? QStringLiteral("Resume") : QStringLiteral("繼續");
    if (key == QStringLiteral("batch_pause_tooltip")) {
        return en ? QStringLiteral("Pause between files — the current file finishes, the queue waits")
                  : QStringLiteral("在檔案之間暫停——當前檔案會完成分析，佇列保留等待繼續");
    }
    if (key == QStringLiteral("batch_paused_notice"))
        return en ? QStringLiteral("⏸️ Batch analysis paused — queue preserved")
                  : QStringLiteral("⏸️ 批次分析已暫停，佇列已保留");
    if (key == QStringLiteral("batch_resumed_notice"))
        return en ? QStringLiteral("▶️ Batch analysis resumed")
                  : QStringLiteral("▶️ 批次分析已繼續");
    if (key == QStringLiteral("batch_eta_sec"))
        return en ? QStringLiteral(" — about %1s left") : QStringLiteral("（預估剩餘約 %1 秒）");
    if (key == QStringLiteral("batch_eta_min_sec"))
        return en ? QStringLiteral(" — about %1m %2s left") : QStringLiteral("（預估剩餘約 %1 分 %2 秒）");
    if (key == QStringLiteral("watch_degraded_notice")) {
        return en ? QStringLiteral("⚠️ Live folder monitoring partially disabled (%1 folders exceed the OS watch limit) — refresh manually after external changes")
                  : QStringLiteral("⚠️ 即時資料夾監控已部分停用（%1 個資料夾超出系統監控上限），外部變更後請手動重新整理");
    }
    if (key == QStringLiteral("summary_low_confidence_prefix")) {
        return en ? QStringLiteral("⚠️ [Low confidence — classified mainly by filename] ")
                  : QStringLiteral("⚠️ [低信心：內容不足，主要依檔名推測] ");
    }
    if (key == QStringLiteral("settings_summary_typewriter")) {
        return en ? QStringLiteral("Typewriter animation for AI summaries (off = instant display)")
                  : QStringLiteral("AI 摘要打字機動畫（關閉時立即顯示全文）");
    }
    if (key == QStringLiteral("settings_time_schedule"))
        return en ? QStringLiteral("Run analysis only during scheduled hours")
                  : QStringLiteral("啟用指定時段分析");
    if (key == QStringLiteral("settings_schedule_start"))
        return en ? QStringLiteral("Start time") : QStringLiteral("開始時間");
    if (key == QStringLiteral("settings_schedule_end"))
        return en ? QStringLiteral("End time") : QStringLiteral("結束時間");
    if (key == QStringLiteral("settings_data_group"))
        return en ? QStringLiteral("Data & cache") : QStringLiteral("資料與快取管理");
    if (key == QStringLiteral("settings_clear_ai"))
        return en ? QStringLiteral("Clear AI cache (keep paths & hashes)")
                  : QStringLiteral("清除 AI 分析快取（保留路徑與 Hash）");
    if (key == QStringLiteral("settings_clear_hash"))
        return en ? QStringLiteral("Clear hash records (force SHA-256 recompute)")
                  : QStringLiteral("清除雜湊紀錄（強制重新計算 SHA-256）");
    if (key == QStringLiteral("settings_factory_reset"))
        return en ? QStringLiteral("Factory-reset workspace (delete metadata)")
                  : QStringLiteral("徹底重置工作區（刪除 metadata）");
    if (key == QStringLiteral("settings_save"))
        return en ? QStringLiteral("Save settings") : QStringLiteral("儲存設定");
    if (key == QStringLiteral("settings_dialog_title"))
        return en ? QStringLiteral("⚙️ Settings") : QStringLiteral("⚙️ 設定");

    if (key == QStringLiteral("btn_assign_force_category"))
        return en ? QStringLiteral("✅ Assign category") : QStringLiteral("✅ 指定分類");
    if (key == QStringLiteral("btn_ai_tag_folders")) {
        return en ? QStringLiteral("🤖 AI tag folders (Generate Tag Folders)")
                  : QStringLiteral("🤖 AI 智能標籤分類 (Generate Tag Folders)");
    }
    if (key == QStringLiteral("archive_keep_ai_default")) {
        return en ? QStringLiteral("[Keep AI default (metadata tags)]")
                  : QStringLiteral("[維持 AI 預設分類 (使用 Metadata 標籤)]");
    }
    if (key == QStringLiteral("archive_select_folder"))
        return en ? QStringLiteral("Choose archive destination folder:")
                  : QStringLiteral("選擇歸檔目標資料夾：");

    // --- 18 AI drawer categories (canonical zh_TW keys) ---
    if (key == QStringLiteral("🎓 大學學業與通識"))
        return en ? QStringLiteral("🎓 University & general studies") : key;
    if (key == QStringLiteral("🔬 STEM與醫學專業"))
        return en ? QStringLiteral("🔬 STEM & medical sciences") : key;
    if (key == QStringLiteral("⚖️ 法商與人文社會"))
        return en ? QStringLiteral("⚖️ Law, business & humanities") : key;
    if (key == QStringLiteral("🤖 AI與資料科學"))
        return en ? QStringLiteral("🤖 AI & data science") : key;
    if (key == QStringLiteral("⚙️ 系統與底層開發"))
        return en ? QStringLiteral("⚙️ Systems & low-level dev") : key;
    if (key == QStringLiteral("🌐 網頁與後端開發"))
        return en ? QStringLiteral("🌐 Web & backend dev") : key;
    if (key == QStringLiteral("📱 行動與跨平台開發"))
        return en ? QStringLiteral("📱 Mobile & cross-platform dev") : key;
    if (key == QStringLiteral("✈️ 語文檢定與留學"))
        return en ? QStringLiteral("✈️ Language tests & study abroad") : key;
    if (key == QStringLiteral("🏖️ 旅遊與生活票證"))
        return en ? QStringLiteral("🏖️ Travel & life documents") : key;
    if (key == QStringLiteral("💼 商務與職場管理"))
        return en ? QStringLiteral("💼 Business & workplace") : key;
    if (key == QStringLiteral("🎨 設計與影音剪輯"))
        return en ? QStringLiteral("🎨 Design & video editing") : key;
    if (key == QStringLiteral("📝 一般文件與排版"))
        return en ? QStringLiteral("📝 General documents & layout") : key;
    if (key == QStringLiteral("🗄️ 數據工程與資料庫"))
        return en ? QStringLiteral("🗄️ Data engineering & databases") : key;
    if (key == QStringLiteral("📊 財務與試算表"))
        return en ? QStringLiteral("📊 Finance & spreadsheets") : key;
    if (key == QStringLiteral("🌱 農業環境與食品"))
        return en ? QStringLiteral("🌱 Agriculture, environment & food") : key;
    if (key == QStringLiteral("📚 教育師培與教學"))
        return en ? QStringLiteral("📚 Education & teaching") : key;
    if (key == QStringLiteral("🛡️ 軍事與國防政策"))
        return en ? QStringLiteral("🛡️ Military & defense policy") : key;
    if (key == QStringLiteral("🎮 遊戲與互動開發"))
        return en ? QStringLiteral("🎮 Games & interactive dev") : key;
    if (key == QStringLiteral("📥 暫存與系統備份"))
        return en ? QStringLiteral("📥 Staging & system backups") : key;
    if (key == QStringLiteral("📦 雜項"))
        return en ? QStringLiteral("📦 Miscellaneous") : key;

    if (key == QStringLiteral("redundancy_delete_none")) {
        return en ? QStringLiteral("No files were deleted. Check permissions or that paths still exist.")
                  : QStringLiteral("沒有檔案被刪除。請確認路徑仍存在且具備寫入／刪除權限。");
    }
    if (key == QStringLiteral("redundancy_delete_select_first")) {
        return en ? QStringLiteral("Please check one or more file paths to delete, then try again.")
                  : QStringLiteral("請先勾選要刪除的檔案路徑，再按刪除。");
    }
    if (key == QStringLiteral("redundancy_delete_success")) {
        return en ? QStringLiteral("Redundant files cleaned up successfully!\nDeleted:\n%1")
                  : QStringLiteral("成功清理冗餘檔案！\n已刪除以下檔案：\n%1");
    }
    if (key == QStringLiteral("dialog_close")) return en ? QStringLiteral("Close") : QStringLiteral("關閉");

    if (key == QStringLiteral("當前目錄包含過多檔案（共 %1 個），繪製完整關聯圖可能導致畫面雜亂或系統卡頓。是否僅顯示核心標籤與前 50 個關聯檔案？")) {
        return en ? QStringLiteral("The current folder contains too many files (total %1). Rendering the full graph may be cluttered or slow. Show only core tags and the first 50 related files?")
                  : QStringLiteral("當前目錄包含過多檔案（共 %1 個），繪製完整關聯圖可能導致畫面雜亂或系統卡頓。是否僅顯示核心標籤與前 50 個關聯檔案？");
    }

    if (key == QStringLiteral("workspace_clear_busy")) {
        return en ? QStringLiteral("Please wait until the current AI analysis finishes, then try again.")
                  : QStringLiteral("請等待目前 AI 分析完成後再執行資料清除。");
    }
    if (key == QStringLiteral("workspace_clear_ai_done")) {
        return en ? QStringLiteral("AI tags and analysis cache cleared (paths and content hashes kept).")
                  : QStringLiteral("已清除 AI 標籤與分析快取（已保留檔案路徑與內容雜湊）。");
    }
    if (key == QStringLiteral("workspace_clear_hash_done")) {
        return en ? QStringLiteral("SHA-256 and hash analysis cache cleared. Next analysis will re-hash files.")
                  : QStringLiteral("已清除雜湊紀錄與雜湊分析快取，下次分析將重新計算 SHA-256。");
    }
    if (key == QStringLiteral("workspace_factory_stop_batch")) {
        return en ? QStringLiteral("Stop folder batch analysis before resetting the workspace.")
                  : QStringLiteral("請先停止資料夾分析，再執行工作區重置。");
    }
    if (key == QStringLiteral("workspace_factory_done")) {
        return en ? QStringLiteral("Workspace metadata was reset.")
                  : QStringLiteral("工作區 metadata 已重置。");
    }

    return key;
}

QString LanguageManager::localizedDrawerLabel(const QString &drawerKey) const
{
    const QString k = drawerKey.trimmed();
    if (k.isEmpty())
        return k;
    const QString localized = getText(k);
    return localized;
}


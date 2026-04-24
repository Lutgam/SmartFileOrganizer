# Smart File Organizer - 系統架構與開發備忘錄
**最後更新：2026-04-15 (第十二波資料流重構與修復完成)**

## 📌 當前系統狀態 (Current System State)
本專案為跨平台 C++/Qt6 桌面應用程式，目前已具備「商業級」的基礎檔案管理與 AI 標籤推論能力。
核心編譯已通過 (`make -j4` Exit Code 0)，運作極度穩定。

## 🏗️ 已確立且不可更動的核心架構 (Immutable Architecture)
1. **視圖架構 (View Layer)**：
   - 堅守 `QListWidget` (Item-based) 實作左側標籤庫與中間檔案列表。
   - **絕對禁止**引入 `QSortFilterProxyModel` 或重構為 `QListView`，以避免破壞現有右鍵選單 (Context Menu) 與資料連動。
   - 搜尋、排序、標籤點擊過濾，皆由自訂的 `sortAndFilterFiles()` 負責，透過 `item->setHidden()` 進行毫秒級的 UI 更新。
2. **AI 引擎 (LLM Infrastructure)**：
   - 使用 `LlamaEngine.cpp` 呼叫原生 `llama.cpp`。
   - 採用 Qt 非同步執行緒 (`QtConcurrent`) 載入大模型，避免 UI 阻塞。
3. **字串清洗與資料庫去重 (Data Sanitization)**：
   - AI 標籤生成後，僅使用安全的 `QString::replace()` 清除 `。`, `.`, `、`, `\n` 與空白。
   - **嚴禁使用**會誤刪中文字的 Regex (例如 `[^\w]`)。
4. **生命週期與預分類 (Lifecycle)**：
   - 載入資料夾的瞬間，系統會自動根據副檔名賦予預設標籤（需帶 Emoji，如 `"🖼️ 圖片"`），並同步寫入 `TagManager`。

## 🎯 下一步開發目標 (Next Milestone)
- **多模態視覺大腦 (Multimodal Vision Engine)**：
  - 放棄依賴外部的 OpenCV/ONNX 架構（避免跨平台編譯地獄）。
  - 計畫將現有的 `LlamaEngine` 升級，載入支援視覺的多模態模型 (如 LLaVA / Qwen-VL GGUF)。
  - 目標：將圖片二進位資料直接餵給 LlamaEngine，實現真正的「看圖生標籤」。
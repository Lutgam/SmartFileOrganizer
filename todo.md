# 專題待辦事項清單 (Project To-Do List)

這份清單彙整自 `report.md` 與 `supported_formats.txt`，列出尚未實作的功能與格式支援。

## 1. 檔案格式支援 (Supported Formats - [Pending])
依據 `supported_formats.txt` 尚未完成項目：

### 文件類
- [ ] .pdf (需整合 Poppler 或相關 PDF 解析庫)
- [ ] .eml (Email 格式)

### 多媒體 - 圖片
- [ ] .jpg / .jpeg
- [ ] .png
- [ ] .heic
- [ ] .gif (含動畫 GIF)

### 多媒體 - 音訊
- [ ] .mp3
- [ ] .wav
- [ ] .aac
- [ ] .m4a

### 多媒體 - 影片
- [ ] .mp4
- [ ] .mkv
- [ ] .mov

### 壓縮檔
- [ ] .zip
- [ ] .rar

## 2. 核心功能開發 - 混合搜尋 (Hybrid Search)
依據 `report.md` [實作步驟規劃 - 階段一]：

### 後端架構
- [ ] 建立 `HybridSearch` 類別 (`src/core/HybridSearch.h`, `.cpp`)
- [ ] 實作 `Search(query_string)` 函式：
    - [ ] 關鍵字搜尋：整合 SQLite FTS5 查詢
    - [ ] 向量搜尋：呼叫 LlamaEngine 轉換 query 向量 -> 查詢 VectorIndex
    - [ ] 結果融合：實作 RRF 演算法合併結果並排序
- [ ] 資料同步機制：
    - [ ] 修改 `DirectoryWatcher`，確保新增檔案時同時寫入 DatabaseManager (FTS) 與 VectorIndex

### 前端整合
- [ ] 修改 `MainWindow::filterFiles`，於搜尋時呼叫 HybridSearch
- [ ] UI 調整：設計搜尋結果顯示區域 (List View 或 Filter Mode TreeView)

## 3. 視覺引擎 (Vision Infrastructure)
依據 `report.md` [實作步驟規劃 - 階段二]：

### 環境建置
- [ ] 下載依賴庫：`opencv_world.dll`, `onnxruntime.dll`
- [ ] 下載 CLIP 模型：`clip-image-encoder.onnx`, `clip-text-encoder.onnx`
- [ ] 更新 `CMakeLists.txt` 以連結上述庫

### 引擎實作 (src/ai/VisionEngine)
- [ ] 圖片預處理：Resize (224x224) -> Normalize
- [ ] 推論 (Inference)：輸入 ONNX 模型 -> 輸出 512維 Feature Vector

### 系統整合
- [ ] `FileScanner` / `DirectoryWatcher`：
    - [ ] 新增對 .jpg/.png 的支援
    - [ ] 呼叫 `VisionEngine` 提取特徵 -> 存入 `VectorIndex` (標記為 Image Vector)
- [ ] 搜尋升級：
    - [ ] 支援「文字搜圖」 (Text Query -> Image Vector)
    - [ ] 支援「以圖搜圖」 (Image Query -> Image Vector)

## 4. 介面整合與系統測試 (Phase 3 & 4)
- [ ] **關聯視圖 (Graph View)**：完成力導向圖與檔案節點的完整互動
- [ ] **整合測試**：整合前端 TreeView/Preview/Graph 與後端分析模組
- [ ] **效能測試**：針對大型檔案與大量檔案的掃描速度測試

## 5. 未來展望
- [ ] 自動化背景排程分析
- [ ] 向量問答 (QA)

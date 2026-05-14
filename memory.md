# 🧠 開發記憶與架構日誌 (Architecture & Development Memory)

本文件記錄了 SmartFile Organizer 在開發過程中的核心架構演進、效能瓶頸突破以及關鍵技術決策 (ADR)。

## 🎯 核心架構演進

### 1. 邊緣推論與視圖解耦 (UI/AI Decoupling)
早期版本在載入目錄時自動觸發 AI 全域掃描，導致巨量 I/O 請求阻塞 Qt Main Thread，引發嚴重的 UI 凍結 (Freeze)。
* **重構決策**: 徹底剝離「視圖渲染」與「背景分析」。導入 `QtConcurrent` 任務佇列與手動觸發機制，確保目錄切換的 O(1) 體感瞬間載入。

### 2. 增量反饋與串流更新 (Incremental Streaming UI) [Sprint 13.0]
為解決長時間批次分析造成的「資訊黑盒子」等待焦慮，重構了狀態同步管線。
* **技術實作**: 任務細粒度化。單檔分析完成後即時透過 `Qt::QueuedConnection` 發出 `fileAnalysisFinished` 信號，觸發局部 UI 刷新 (跑馬燈標籤)。並將高耗能的全域冗餘報告延遲至佇列清零時才觸發。

### 3. 多維度冗餘比對演算法 (Multi-Dimensional Redundancy Check)
傳統的 SHA-256 Hash 比對雖然安全，但無法抓出「使用者將同份報告存成 PDF 與 PPTX」這類痛點。
* **技術實作**: 升級比對管線，首層先透過 `QFileInfo::completeBaseName()` 進行主檔名分組，次層再比對 Hash。精準隔離出「內容相同」與「檔名相同但內容不同」兩大類別，確保不誤刪異質備份檔。

### 4. 生命週期與快取記憶恢復 (Lifecycle State Restoration) [Sprint 13.1]
邊緣 AI 推論成本極高，系統必須具備極強的狀態持久化能力。
* **技術實作**: 導入 `metadata.json` 與全域 Hash Cache。並於 `loadWorkspace` 階段實作 `restorePersistedAnalysisUiState()`，確保系統重啟時能毫秒級重建 AI 標籤樹與預覽視圖，達成「無感重啟」。

### 5. 防禦性字串淨化 (Defensive String Sanitization)
處理真實世界檔案 (尤其是 PDF 萃取文本) 時，常夾帶不可見字元與 Null bytes (`\0`)，這會導致底層 Llama JSON Parser 崩潰。
* **技術實作**: 實作 `sanitizeTextForAi()` 管線，在將字串餵給 LLM 之前，強制濾除控制字元與亂碼，並嚴格截斷至 1800 字元，徹底保護記憶體與模型 Context Window。
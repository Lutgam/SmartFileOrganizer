# 🗺️ SmartFile Organizer - 未來開發藍圖 (Roadmap & TODO)

雖然目前系統已具備完整的邊緣 AI 推論與檔案管理能力，但為了因應更龐大與複雜的企業級需求，未來預計進行以下階段性升級：

## 🚀 Phase 1: 深度語意與搜尋強化 (Deep Semantics & Search)
- [ ] **導入向量資料庫 (Vector Database)**: 整合 FAISS 或 ChromaDB，將目前的「標籤關鍵字匹配」升級為真正的「自然語言語意搜尋 (Semantic Search)」，允許使用者輸入完整白話文來尋找檔案。
- [ ] **OCR 影像辨識整合**: 針對圖片掃描檔 (Scanned PDF, JPG)，於背景引入輕量化 Tesseract OCR 引擎，確保無文本檔案也能進行精準的 LLM 語意分析。
- [ ] **多模態推論 (Multimodal AI)**: 升級 Llama 模型以支援 Vision 視覺能力，直接針對圖片與圖表內容生成摘要。

## 🎨 Phase 2: 進階體驗與工作流 (Advanced UX & Workflow)
- [ ] **全域深色模式 (Dark Mode)**: 遵循作業系統色彩定義，提供完整的 Light/Dark 主題切換。
- [ ] **智慧拖曳歸檔 (Smart Drag & Drop)**: 允許使用者將檔案直接拖曳至左側的「虛擬 AI 標籤樹」上，系統自動建立實體捷徑或進行搬移。
- [ ] **自訂分類訓練 (Custom LUT Builder)**: 提供視覺化介面，讓使用者能自由微調 `categories_config.json`，打造專屬的 AI 字典。

## ⚙️ Phase 3: 底層效能與跨平台 (Performance & Cross-Platform)
- [ ] **GPU 加速支援 (Vulkan / Metal / CUDA)**: 根據不同作業系統，動態切換推論後端，進一步將單檔分析時間壓縮至毫秒級。
- [ ] **記憶體動態卸載 (Memory Swapping)**: 針對超大型工作區，實作模型記憶體的動態載入與釋放機制，降低背景常駐的 RAM 佔用。
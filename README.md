# 📂 SmartFile Organizer (智慧檔案管理系統)

![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Windows-lightgrey.svg)
![C++](https://img.shields.io/badge/C++-17%2B-00599C?logo=c%2B%2B)
![Qt](https://img.shields.io/badge/Qt-6.x-41CD52?logo=qt)
![AI](https://img.shields.io/badge/Edge%20AI-Llama.cpp-FF9900)

SmartFile Organizer 是一款基於 **C++/Qt** 與 **Edge AI (本地邊緣推論)** 技術打造的新世代智慧檔案管理工具。透過內建的本地端 LLM (大型語言模型)，系統能自動深度解析檔案語意、生成精準摘要與標籤，並進行全自動的分類歸檔，徹底解決傳統檔案管理中「找不到、懶得理、重複存」的三大痛點。

## ✨ 核心特色 (Key Features)

* **🧠 邊緣 AI 語意引擎 (Edge AI Engine)**
  * 內建基於 `Llama.cpp` 的本地推論引擎，完全離線運行，確保使用者隱私資料不外洩。
  * 支援 18 大維度動態分類 (LUT Routing)，自動解析檔案內容並賦予多維度語意標籤與摘要。
* **🚀 非同步串流與極致效能 (Async Streaming & Performance)**
  * 實作多執行緒與 `QtConcurrent` 任務佇列，確保 AI 運算時 UI 絕對流暢 (Anti-Jitter)。
  * 獨創 **Metadata Hash 快取機制**：首次分析後即建立指紋特徵，未來載入時間趨近於 0 秒。
* **🗑️ 工業級冗餘比對 (Industrial-grade Redundancy Check)**
  * 基於 SHA-256 二進位特徵值比對，精準抓出「散落各處、檔名不同但靈魂相同」的無效佔用檔案。
  * 具備多維度分組邏輯，能安全分辨「同檔名但不同格式 (如 PDF vs PPTX)」的邊緣情境。
* **🎨 現代化沉浸式體驗 (Immersive UX)**
  * 支援側邊導覽 (Sidebar Navigation)、標籤樹狀圖、即時狀態跑馬燈與預覽控制區。
  * 具備高度防呆機制 (I/O 阻斷、字串淨化、路徑鎖定)。

## 🛠️ 技術堆疊 (Tech Stack)

* **GUI 框架**: Qt 6 (Widgets / QSS)
* **核心語言**: Modern C++ (C++17)
* **AI 引擎**: Llama.cpp (GGUF Format)
* **資料持久化**: JSON (Metadata & Config) / QSettings

## 🚀 快速啟動 (Quick Start)

1. 克隆本專案：`git clone https://github.com/Lutgam/SmartFileOrganizer.git`
2. 準備 AI 模型：將相容的 `.gguf` 模型放置於 `./models/` 目錄下。
3. 編譯環境：使用 CMake 或 Qt Creator 開啟專案進行編譯 (建議使用 Release 模式以獲得最佳推論效能)。
4. 啟動並載入您需要整理的工作區資料夾，點擊「▶️ 開始 AI 智能分析」。

## 授權條款 (License)

本專案採用 **PolyForm Noncommercial License 1.0.0** 授權。

您可以免費將本專案的原始碼用於**個人、學術研究、教育及非營利目的**。
**嚴禁任何未經授權的商業使用**（包含但不限於：整合至商業軟體、企業內部業務使用、作為付費服務的一部分提供等）。

詳細的授權條款，請參閱本專案根目錄下的 [LICENSE](LICENSE.md) 檔案。

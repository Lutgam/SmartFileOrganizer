// SmartFile Organizer — headless evaluation runner.
//
//   sfo_eval --generate <corpus_dir>
//       Write ~100 synthetic labelled files + manifest.json.
//
//   sfo_eval --run <corpus_dir> --model <model.gguf> [--limit N]
//       Analyze every manifest entry through the same pipeline the app uses
//       (DocumentParser -> LlamaEngine::suggestTags -> drawer LUT routing) and
//       write eval_results.json + eval_report.md with:
//         - drawer routing accuracy
//         - expected-keyword hit rate
//         - raw JSON validity rate (measures GBNF constrained decoding)
//         - throughput (sec/file, files/min) and peak RSS
//
// The manifest format doubles as the import format for hand-labelled real
// corpora: point --run at any directory containing manifest.json.

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "ai/LlamaEngine.h"
#include "core/DocumentParser.h"
#include "core/DrawerCategoryLut.h"

#include <cstdio>

#if defined(Q_OS_DARWIN) || defined(Q_OS_LINUX)
#  include <sys/resource.h>
#endif

namespace {

struct CorpusFile {
    const char *name;     // filename (relative to corpus dir)
    const char *content;  // file body (UTF-8)
};

struct CorpusCategory {
    const char *expectedDrawer;
    const char *keywordsCsv;  // acceptable tag keywords, comma separated
    std::initializer_list<CorpusFile> files;
};

// 10 categories x 10 files = 100 labelled cases. Filenames and bodies are
// designed so a correct analysis routes to exactly one non-misc drawer.
const CorpusCategory kCorpus[] = {
    {"🎓 大學學業與通識", "期末考,期中考,考古題,解答,作業,筆記,講義,課程",
     {{"作業系統_期末考_解答.txt", "本檔案為作業系統課程的期末考解答，包含行程排程、死結預防與虛擬記憶體的申論題詳解。"},
      {"資料結構_第三次作業.txt", "資料結構課程第三次作業：實作二元搜尋樹的插入與刪除，並分析時間複雜度。"},
      {"普通心理學_課堂筆記_W5.txt", "普通心理學第五週課堂筆記，主題為古典制約與操作制約的差異與實驗設計。"},
      {"計算機概論_考古題_110.txt", "110 學年度計算機概論期中考古題，含選擇題與簡答題，附參考解答。"},
      {"離散數學_講義_圖論.txt", "離散數學課程講義：圖論基礎，涵蓋尤拉路徑、漢米爾頓迴圈與著色問題。"},
      {"程式設計_期中考重點整理.txt", "程式設計期中考重點整理：指標、遞迴、結構體與動態記憶體配置。"},
      {"通識_藝術概論_期末報告.txt", "通識課程藝術概論期末報告草稿，主題為印象派繪畫的光影表現。"},
      {"線性代數_第二章作業解答.txt", "線性代數第二章作業解答：矩陣秩、列空間與零空間的計算過程。"},
      {"統計學_小考二_試題.txt", "統計學第二次小考試題：假設檢定、信賴區間與 p 值的觀念題。"},
      {"微積分_期末總複習講義.txt", "微積分期末總複習講義，整理極限、微分、積分與級數收斂的核心定理。"}}},
    {"🔬 STEM與醫學專業", "微積分,線性代數,物理,化學,解剖,生理,藥理,熱力學,電磁學,工程",
     {{"電磁學_馬克士威方程式推導.txt", "推導馬克士威方程組的微分形式，說明電場與磁場的交互關係與電磁波傳播。"},
      {"熱力學_卡諾循環分析.txt", "卡諾循環的效率推導與熵變分析，比較理想氣體在等溫與絕熱過程的行為。"},
      {"解剖學_上肢神經支配整理.txt", "上肢神經支配整理：臂神經叢的分支、正中神經與尺神經的支配肌群。"},
      {"藥理學_抗生素機轉比較.txt", "比較 β-內醯胺類與巨環內酯類抗生素的作用機轉、抗藥性與副作用。"},
      {"生理學_心臟電位傳導.txt", "心臟電位傳導路徑：竇房結、房室結與浦金氏纖維的去極化順序。"},
      {"有機化學_親核取代反應.txt", "SN1 與 SN2 親核取代反應的機構比較、立體化學與溶劑效應。"},
      {"工程力學_樑的剪力彎矩圖.txt", "簡支樑受集中載重時的剪力圖與彎矩圖繪製步驟與內力計算。"},
      {"近代物理_光電效應實驗.txt", "光電效應實驗數據分析，驗證普朗克常數並討論光子能量量子化。"},
      {"普通化學_化學平衡常數.txt", "化學平衡常數的計算與勒沙特列原理應用，含溫度與壓力的影響。"},
      {"病理學_發炎反應分期.txt", "急性發炎與慢性發炎的病理機轉、細胞激素參與及組織修復過程。"}}},
    {"⚖️ 法商與人文社會", "民法,刑法,憲法,經濟,哲學,心理學,歷史,社會學,行銷,企管",
     {{"民法總則_意思表示筆記.txt", "民法總則中意思表示的成立要件、錯誤撤銷與虛偽表示的法律效果。"},
      {"刑法分則_竊盜罪構成要件.txt", "刑法分則竊盜罪的構成要件分析：主觀不法意圖與持有移轉的認定。"},
      {"個體經濟學_供需彈性.txt", "供給與需求價格彈性的計算、影響因素與課稅歸宿分析。"},
      {"西洋哲學史_康德批判哲學.txt", "康德三大批判的核心問題：純粹理性、實踐理性與判斷力的界限。"},
      {"社會學_符號互動論摘要.txt", "符號互動論的理論脈絡：米德的自我理論與高夫曼的戲劇論。"},
      {"行銷管理_STP策略分析.txt", "行銷管理 STP 策略：市場區隔、目標市場選擇與品牌定位實例。"},
      {"憲法_違憲審查制度比較.txt", "比較集中式與分散式違憲審查制度，分析我國憲法法庭的運作。"},
      {"近代史_明治維新影響.txt", "明治維新對東亞政治格局的影響：中央集權、殖產興業與軍制改革。"},
      {"發展心理學_皮亞傑理論.txt", "皮亞傑認知發展四階段理論與具體運思期的守恆概念實驗。"},
      {"企業管理_波特五力分析.txt", "以波特五力模型分析產業競爭結構：供應商、買方與替代品威脅。"}}},
    {"🤖 AI與資料科學", "機器學習,深度學習,模型,pytorch,tensorflow,dataset,訓練,微調,nlp,lora",
     {{"finetune_llama_lora.py", "# LoRA fine-tuning script for Llama models\n# 使用 peft 與 transformers 進行模型微調\nimport torch\nfrom peft import LoraConfig\nlora_config = LoraConfig(r=8, lora_alpha=32)\n"},
      {"機器學習_過擬合對策筆記.txt", "整理機器學習過擬合的對策：正則化、dropout、資料增強與早停法。"},
      {"transformer_attention_實作.py", "# Multi-head attention 實作\nimport torch.nn as nn\nclass MultiHeadAttention(nn.Module):\n    def __init__(self, d_model, num_heads): ...\n"},
      {"dataset_前處理流程.txt", "訓練資料集前處理流程：缺值填補、標準化、類別編碼與訓練驗證切分。"},
      {"深度學習_CNN架構比較.txt", "比較 ResNet、VGG 與 EfficientNet 的卷積架構設計與參數效率。"},
      {"nlp_詞嵌入技術整理.txt", "NLP 詞嵌入技術整理：word2vec、GloVe 與上下文化嵌入的差異。"},
      {"模型量化_int8_實驗紀錄.txt", "模型 int8 量化實驗：對推論速度與準確率的影響，含校準資料選擇。"},
      {"pytorch_訓練迴圈範本.py", "# PyTorch training loop template\nfor epoch in range(epochs):\n    for batch in dataloader:\n        optimizer.zero_grad()\n        loss.backward()\n"},
      {"強化學習_QLearning筆記.txt", "Q-Learning 與 DQN 的差異：經驗回放、目標網路與探索策略設計。"},
      {"資料科學_特徵工程心法.txt", "特徵工程實務：交互特徵、時間窗聚合與目標編碼的洩漏風險。"}}},
    {"⚙️ 系統與底層開發", "kernel,linux,docker,compiler,cmake,編譯,rust,系統,驅動,devops",
     {{"kernel_module_hello.c", "// Linux kernel module example\n#include <linux/module.h>\n#include <linux/kernel.h>\nstatic int __init hello_init(void) { return 0; }\n"},
      {"docker_compose_部署筆記.txt", "Docker Compose 多容器部署筆記：網路設定、volume 掛載與健康檢查。"},
      {"cmake_跨平台建置設定.txt", "CMake 跨平台建置設定：FetchContent 管理依賴與多組態產生器。"},
      {"rust_所有權系統筆記.txt", "Rust 所有權與借用檢查：生命週期標註與移動語意的常見錯誤。"},
      {"linux_系統呼叫追蹤.txt", "使用 strace 追蹤 Linux 系統呼叫，分析檔案 I/O 與行程建立開銷。"},
      {"compiler_詞法分析器實作.txt", "編譯器前端詞法分析器實作：有限狀態機與正規表達式轉換。"},
      {"驅動程式_中斷處理機制.txt", "裝置驅動程式的中斷處理：上半部與下半部、tasklet 與 workqueue。"},
      {"devops_CI管線設計.txt", "DevOps CI 管線設計：建置快取、平行測試與部署閘門的實務配置。"},
      {"記憶體配置器_實作分析.c", "// Custom memory allocator\n// 實作 free list 與 slab 配置策略\nvoid *my_malloc(size_t size);\n"},
      {"作業系統_排程器演算法.txt", "作業系統排程器演算法比較：CFS、Round-Robin 與優先權反轉問題。"}}},
    {"🌐 網頁與後端開發", "react,vue,django,flask,api,frontend,backend,node,css,html",
     {{"react_hooks_狀態管理.txt", "React Hooks 狀態管理實務：useState、useEffect 依賴陣列與自訂 hook。"},
      {"django_rest_api_設計.txt", "Django REST framework API 設計：序列化器、視圖集與權限控制。"},
      {"user_auth_middleware.ts", "// Express auth middleware\nimport jwt from 'jsonwebtoken';\nexport function requireAuth(req, res, next) { ... }\n"},
      {"css_grid_排版技巧.txt", "CSS Grid 排版技巧：自適應欄位、區域命名與與 Flexbox 的取捨。"},
      {"vue_組件通訊整理.txt", "Vue 組件通訊方式整理：props、emit、provide/inject 與 Pinia 狀態。"},
      {"nodejs_事件迴圈解析.txt", "Node.js 事件迴圈解析：微任務、巨任務與 I/O 回呼的執行順序。"},
      {"api_版本管理策略.txt", "REST API 版本管理策略：URL 版本、Header 協商與向後相容設計。"},
      {"frontend_效能優化清單.txt", "前端效能優化：程式碼分割、圖片延遲載入與 Core Web Vitals 指標。"},
      {"flask_藍圖架構範例.py", "# Flask blueprint structure\nfrom flask import Blueprint\nbp = Blueprint('users', __name__)\n@bp.route('/users')\ndef list_users(): ...\n"},
      {"html_語意化標籤指南.txt", "HTML 語意化標籤指南：header、article、section 的正確使用與無障礙。"}}},
    {"📱 行動與跨平台開發", "android,kotlin,ios,swift,flutter,app,行動,mobile",
     {{"MainActivity.kt", "// Android entry activity\npackage com.example.app\nimport android.os.Bundle\nclass MainActivity : AppCompatActivity() { }\n"},
      {"flutter_widget_生命週期.txt", "Flutter StatefulWidget 生命週期：initState、build 與 dispose 的時機。"},
      {"ios_swiftui_導航設計.txt", "SwiftUI NavigationStack 導航設計：路徑管理與深層連結處理。"},
      {"android_room_資料庫整合.txt", "Android Room 資料庫整合：Entity、DAO 與遷移策略的實務範例。"},
      {"kotlin_協程併發模式.txt", "Kotlin 協程併發模式：structured concurrency、Flow 與例外傳播。"},
      {"app_上架審核清單.txt", "行動 App 上架審核清單：隱私權聲明、權限說明與截圖規格。"},
      {"swift_記憶體管理ARC.txt", "Swift ARC 記憶體管理：強弱引用、循環引用與 capture list。"},
      {"跨平台_框架選型比較.txt", "行動跨平台框架選型：Flutter、React Native 與原生開發的取捨。"},
      {"android_jetpack_compose筆記.txt", "Jetpack Compose 宣告式 UI：remember、state hoisting 與重組最佳化。"},
      {"mobile_推播通知整合.txt", "行動推播通知整合：FCM 與 APNs 的憑證設定與背景處理。"}}},
    {"✈️ 語文檢定與留學", "托福,雅思,多益,TOEIC,GRE,推薦信,簽證,留學,單字,sop",
     {{"托福_口說模板整理.txt", "托福口說第一題與第二題的答題模板，含轉折語與時間分配建議。"},
      {"雅思_寫作Task2_範文.txt", "雅思寫作 Task 2 範文解析：論點開展、同義替換與高分句型。"},
      {"GRE_填空高頻單字.txt", "GRE 填空高頻單字整理：六選二同義詞配對與語境判斷技巧。"},
      {"留學_SOP_草稿_v3.txt", "留學申請 SOP 草稿：研究動機、學術背景與選校理由的段落安排。"},
      {"推薦信_教授範本.txt", "學術推薦信範本：課堂表現、研究能力與人格特質的具體事例。"},
      {"多益_聽力Part3_筆記.txt", "多益聽力 Part 3 對話題型筆記：預讀題目與關鍵字定位策略。"},
      {"美國簽證_F1_面試準備.txt", "F1 學生簽證面試準備：財力證明、學習計畫與常見問題應答。"},
      {"雅思_單字_學術分類.txt", "雅思學術單字分類整理：環境、科技、教育與健康主題詞彙。"},
      {"留學_選校評估表.txt", "留學選校評估表：排名、學費、地點、研究方向與獎學金機會。"},
      {"托福_閱讀長難句解析.txt", "托福閱讀長難句解析：插入語、倒裝與指代關係的拆解方法。"}}},
    {"🏖️ 旅遊與生活票證", "機票,車票,訂房,行程,門票,護照,旅遊,住宿,hotel",
     {{"東京五日_行程規劃.txt", "東京五日自由行行程規劃：淺草、澀谷、築地市場與一日近郊路線。"},
      {"長榮航空_電子機票_TPE-NRT.txt", "長榮航空電子機票確認：台北桃園至東京成田，含航廈與行李額度。"},
      {"京都_飯店訂房確認.txt", "京都車站前飯店訂房確認信：入住退房時間、房型與取消政策。"},
      {"環球影城_門票QR.txt", "大阪環球影城一日門票電子憑證，含快速通關使用說明。"},
      {"高鐵_台北左營_車票.txt", "高鐵台北至左營車票訂位紀錄：車次、座位與乘車時間。"},
      {"護照更新_申請須知.txt", "護照效期更新申請須知：應備文件、規費與工作天數。"},
      {"沖繩_租車自駕_行程.txt", "沖繩租車自駕行程：國際駕照、美麗海水族館與古宇利島路線。"},
      {"歐洲_跨國火車_訂票紀錄.txt", "歐洲跨國火車訂票紀錄：巴黎至蘇黎世的訂位車廂與轉乘資訊。"},
      {"民宿_墾丁_住宿確認.txt", "墾丁海景民宿住宿確認：入住日期、訂金收據與接駁說明。"},
      {"日本_溫泉旅館_預約信.txt", "箱根溫泉旅館預約確認信：一泊二食方案與露天風呂使用時段。"}}},
    {"📊 財務與試算表", "發票,報稅,財報,損益,所得稅,預算,會計,試算,報表,invoice",
     {{"2024_綜合所得稅_試算.txt", "2024 年度綜合所得稅試算：標準扣除額、列舉扣除與級距稅率計算。"},
      {"公司_季度損益表_Q3.txt", "公司第三季損益表摘要：營收、毛利率、營業費用與稅後淨利。"},
      {"電子發票_載具歸戶說明.txt", "電子發票手機條碼載具歸戶說明與中獎獎金自動匯款設定。"},
      {"個人_月度預算規劃.txt", "個人月度預算規劃：固定支出、儲蓄率與緊急預備金目標。"},
      {"報稅_扣繳憑單整理.txt", "年度報稅扣繳憑單整理清單：薪資、利息與股利所得明細。"},
      {"公司_資產負債表_年報.txt", "年度資產負債表摘要：流動資產、長期負債與股東權益變動。"},
      {"廠商_報價發票_2024-031.txt", "廠商報價發票 2024-031：品項單價、稅額與付款條件三十天。"},
      {"記帳_現金流量表_範本.txt", "現金流量表範本：營業、投資與籌資活動的現金流分類。"},
      {"投資_股利所得_試算.txt", "股利所得課稅試算：合併計稅與分離課稅的稅負比較。"},
      {"年度_財務報表_分析筆記.txt", "年度財務報表分析筆記：流動比率、負債比與股東權益報酬率。"}}},
};

bool generateCorpus(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        fprintf(stderr, "Cannot create corpus dir: %s\n", qPrintable(dirPath));
        return false;
    }

    QJsonArray manifestFiles;
    int written = 0;
    for (const CorpusCategory &cat : kCorpus) {
        for (const CorpusFile &cf : cat.files) {
            const QString rel = QString::fromUtf8(cf.name);
            QFile f(dir.filePath(rel));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                fprintf(stderr, "Cannot write %s\n", qPrintable(rel));
                return false;
            }
            f.write(QByteArray(cf.content));
            f.close();
            ++written;

            QJsonObject entry;
            entry.insert(QStringLiteral("path"), rel);
            entry.insert(QStringLiteral("expected_drawer"), QString::fromUtf8(cat.expectedDrawer));
            QJsonArray kws;
            for (const QString &k : QString::fromUtf8(cat.keywordsCsv).split(QLatin1Char(',')))
                kws.append(k.trimmed());
            entry.insert(QStringLiteral("keywords"), kws);
            manifestFiles.append(entry);
        }
    }

    QJsonObject manifest;
    manifest.insert(QStringLiteral("version"), 1);
    manifest.insert(QStringLiteral("files"), manifestFiles);
    QFile mf(dir.filePath(QStringLiteral("manifest.json")));
    if (!mf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    mf.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    mf.close();

    printf("Generated %d labelled files + manifest.json in %s\n", written, qPrintable(dirPath));
    return true;
}

qint64 peakRssMb()
{
#if defined(Q_OS_DARWIN)
    struct rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        return static_cast<qint64>(ru.ru_maxrss) / (1024 * 1024); // bytes on macOS
#elif defined(Q_OS_LINUX)
    struct rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        return static_cast<qint64>(ru.ru_maxrss) / 1024; // KB on Linux
#endif
    return -1;
}

struct CaseResult {
    QString path;
    QString expectedDrawer;
    QString predictedDrawer;
    QStringList tags;
    QString summary;
    bool drawerHit = false;
    bool keywordHit = false;
    bool rawJsonValid = false;
    double seconds = 0.0;
};

int runEval(const QString &corpusDir, const QString &modelPath, int limit)
{
    QDir dir(corpusDir);
    QFile mf(dir.filePath(QStringLiteral("manifest.json")));
    if (!mf.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "manifest.json not found in %s (run --generate first)\n", qPrintable(corpusDir));
        return 2;
    }
    const QJsonObject manifest = QJsonDocument::fromJson(mf.readAll()).object();
    const QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    if (files.isEmpty()) {
        fprintf(stderr, "manifest.json contains no files\n");
        return 2;
    }

    LlamaEngine engine;
    printf("Loading model: %s\n", qPrintable(modelPath));
    if (!engine.loadModel(modelPath.toStdString())) {
        fprintf(stderr, "Model load failed\n");
        return 3;
    }

    const SfDrawerCategoryLut lut = SfDrawerCategoryLut::builtinDefault();
    std::vector<CaseResult> results;
    QElapsedTimer total;
    total.start();

    int processed = 0;
    for (const QJsonValue &v : files) {
        if (limit > 0 && processed >= limit)
            break;
        const QJsonObject entry = v.toObject();
        CaseResult r;
        r.path = entry.value(QStringLiteral("path")).toString();
        r.expectedDrawer = entry.value(QStringLiteral("expected_drawer")).toString();
        QStringList keywords;
        for (const QJsonValue &k : entry.value(QStringLiteral("keywords")).toArray())
            keywords << k.toString();

        const QString abs = dir.filePath(r.path);
        const QFileInfo fi(abs);
        if (!fi.exists()) {
            fprintf(stderr, "skip missing file: %s\n", qPrintable(r.path));
            continue;
        }

        // Mirror the app pipeline: extract -> suggestTags -> sanitize -> LUT routing.
        bool pdfMetadataOnly = false;
        const QString content = DocumentParser::extractTextForAi(abs, &pdfMetadataOnly);

        QElapsedTimer t;
        t.start();
        const std::string outJson = engine.suggestTags(
            fi.fileName().toStdString(), content.toStdString(),
            /*rejectedTagsCsv=*/"", /*existingTags=*/"",
            /*contentReadable=*/true, fi.suffix().toLower().toStdString(), pdfMetadataOnly);
        r.seconds = t.elapsed() / 1000.0;

        // Raw validity: did constrained decoding give us parseable JSON pre-sanitizer?
        {
            const QByteArray raw = engine.lastRawSuggestTagsOutput().toUtf8();
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
            r.rawJsonValid = (err.error == QJsonParseError::NoError && doc.isObject()
                              && doc.object().contains(QStringLiteral("summary")));
        }

        const QJsonObject obj = QJsonDocument::fromJson(QByteArray::fromStdString(outJson)).object();
        r.summary = obj.value(QStringLiteral("summary")).toString();
        for (const QJsonValue &tv : obj.value(QStringLiteral("tags")).toArray()) {
            const QString t2 = tv.toString().trimmed();
            if (!t2.isEmpty())
                r.tags << t2;
        }

        // Drawer routing: a file counts as correctly routed when any of its
        // tags lands in the expected drawer (that is how the user finds it).
        for (const QString &tag : r.tags) {
            const QString d = lut.matchText(tag);
            if (r.predictedDrawer.isEmpty() || r.predictedDrawer == QStringLiteral("📦 雜項"))
                r.predictedDrawer = d;
            if (d == r.expectedDrawer) {
                r.predictedDrawer = d;
                r.drawerHit = true;
                break;
            }
        }
        if (!r.drawerHit && lut.matchText(r.summary) == r.expectedDrawer) {
            // Summary routes correctly even if tags do not; count as routed.
            r.predictedDrawer = r.expectedDrawer;
            r.drawerHit = true;
        }

        for (const QString &kw : keywords) {
            for (const QString &tag : r.tags) {
                if (tag.contains(kw, Qt::CaseInsensitive)) {
                    r.keywordHit = true;
                    break;
                }
            }
            if (r.keywordHit)
                break;
        }

        ++processed;
        printf("[%3d/%3lld] %-42s drawer=%s %s json=%s %.1fs\n", processed,
               limit > 0 ? qMin<qint64>(limit, files.size()) : files.size(),
               qPrintable(r.path.left(42)),
               r.drawerHit ? "✓" : "✗",
               r.keywordHit ? "kw✓" : "kw✗",
               r.rawJsonValid ? "✓" : "✗",
               r.seconds);
        fflush(stdout);
        results.push_back(std::move(r));
    }

    if (results.empty()) {
        fprintf(stderr, "No cases processed\n");
        return 4;
    }

    const double totalSec = total.elapsed() / 1000.0;
    int drawerHits = 0, kwHits = 0, jsonValid = 0;
    double sumSec = 0;
    for (const CaseResult &r : results) {
        drawerHits += r.drawerHit ? 1 : 0;
        kwHits += r.keywordHit ? 1 : 0;
        jsonValid += r.rawJsonValid ? 1 : 0;
        sumSec += r.seconds;
    }
    const int n = static_cast<int>(results.size());
    const double drawerAcc = 100.0 * drawerHits / n;
    const double kwRate = 100.0 * kwHits / n;
    const double jsonRate = 100.0 * jsonValid / n;
    const double avgSec = sumSec / n;
    const double filesPerMin = n / (totalSec / 60.0);
    const qint64 rssMb = peakRssMb();

    // results JSON
    {
        QJsonArray arr;
        for (const CaseResult &r : results) {
            QJsonObject o;
            o.insert(QStringLiteral("path"), r.path);
            o.insert(QStringLiteral("expected_drawer"), r.expectedDrawer);
            o.insert(QStringLiteral("predicted_drawer"), r.predictedDrawer);
            o.insert(QStringLiteral("drawer_hit"), r.drawerHit);
            o.insert(QStringLiteral("keyword_hit"), r.keywordHit);
            o.insert(QStringLiteral("raw_json_valid"), r.rawJsonValid);
            o.insert(QStringLiteral("seconds"), r.seconds);
            o.insert(QStringLiteral("summary"), r.summary);
            o.insert(QStringLiteral("tags"), QJsonArray::fromStringList(r.tags));
            arr.append(o);
        }
        QJsonObject root;
        root.insert(QStringLiteral("cases"), arr);
        root.insert(QStringLiteral("drawer_accuracy_pct"), drawerAcc);
        root.insert(QStringLiteral("keyword_hit_rate_pct"), kwRate);
        root.insert(QStringLiteral("raw_json_valid_rate_pct"), jsonRate);
        root.insert(QStringLiteral("avg_seconds_per_file"), avgSec);
        root.insert(QStringLiteral("files_per_minute"), filesPerMin);
        root.insert(QStringLiteral("peak_rss_mb"), rssMb);
        root.insert(QStringLiteral("model"), modelPath);
        QFile rf(dir.filePath(QStringLiteral("eval_results.json")));
        if (rf.open(QIODevice::WriteOnly | QIODevice::Truncate))
            rf.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }

    // Markdown report (drop straight into the final presentation)
    {
        QFile rf(dir.filePath(QStringLiteral("eval_report.md")));
        if (rf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream ts(&rf);
            ts << "# SmartFile Organizer 量化評估報告\n\n";
            ts << "- 模型: `" << QFileInfo(modelPath).fileName() << "`\n";
            ts << "- 樣本數: " << n << " 個含正解標註的檔案(10 類別)\n";
            ts << "- 執行環境: 本地推論(零雲端、零網路)\n\n";
            ts << "| 指標 | 數值 |\n|---|---|\n";
            ts << "| 抽屜分類正確率 | **" << QString::number(drawerAcc, 'f', 1) << "%** (" << drawerHits << "/" << n << ") |\n";
            ts << "| 預期關鍵字命中率 | " << QString::number(kwRate, 'f', 1) << "% |\n";
            ts << "| LLM 原始輸出 JSON 合法率 | " << QString::number(jsonRate, 'f', 1) << "% (GBNF 約束解碼) |\n";
            ts << "| 平均單檔分析時間 | " << QString::number(avgSec, 'f', 2) << " 秒 |\n";
            ts << "| 吞吐量 | " << QString::number(filesPerMin, 'f', 1) << " 檔/分鐘 |\n";
            if (rssMb > 0)
                ts << "| 峰值記憶體 (RSS) | " << rssMb << " MB |\n";
            ts << "\n## 各類別正確率\n\n| 預期抽屜 | 正確 / 總數 |\n|---|---|\n";
            QMap<QString, QPair<int, int>> per;
            for (const CaseResult &r : results) {
                auto &p = per[r.expectedDrawer];
                p.second++;
                if (r.drawerHit) p.first++;
            }
            for (auto it = per.constBegin(); it != per.constEnd(); ++it)
                ts << "| " << it.key() << " | " << it.value().first << " / " << it.value().second << " |\n";
            ts << "\n## 錯誤案例\n\n";
            bool anyMiss = false;
            for (const CaseResult &r : results) {
                if (r.drawerHit) continue;
                anyMiss = true;
                ts << "- `" << r.path << "` 預期 " << r.expectedDrawer
                   << " → 實際 " << (r.predictedDrawer.isEmpty() ? QStringLiteral("(無)") : r.predictedDrawer)
                   << ",tags: " << r.tags.join(QStringLiteral(", ")) << "\n";
            }
            if (!anyMiss)
                ts << "(無)\n";
        }
    }

    printf("\n=== 評估完成 ===\n");
    printf("抽屜分類正確率: %.1f%% (%d/%d)\n", drawerAcc, drawerHits, n);
    printf("關鍵字命中率:   %.1f%%\n", kwRate);
    printf("JSON 合法率:    %.1f%%\n", jsonRate);
    printf("平均單檔:       %.2f 秒 (%.1f 檔/分鐘)\n", avgSec, filesPerMin);
    if (rssMb > 0)
        printf("峰值 RSS:       %lld MB\n", static_cast<long long>(rssMb));
    printf("報告: %s\n", qPrintable(dir.filePath(QStringLiteral("eval_report.md"))));
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("SmartFile"));
    QCoreApplication::setApplicationName(QStringLiteral("SmartFileOrganizerEval"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SmartFile Organizer evaluation runner"));
    parser.addHelpOption();
    const QCommandLineOption optGenerate(QStringLiteral("generate"), QStringLiteral("Generate synthetic corpus into <dir>"), QStringLiteral("dir"));
    const QCommandLineOption optRun(QStringLiteral("run"), QStringLiteral("Run evaluation over corpus <dir> (needs manifest.json)"), QStringLiteral("dir"));
    const QCommandLineOption optModel(QStringLiteral("model"), QStringLiteral("Path to GGUF model"), QStringLiteral("path"));
    const QCommandLineOption optLimit(QStringLiteral("limit"), QStringLiteral("Only evaluate first N cases"), QStringLiteral("n"), QStringLiteral("0"));
    parser.addOption(optGenerate);
    parser.addOption(optRun);
    parser.addOption(optModel);
    parser.addOption(optLimit);
    parser.process(app);

    if (parser.isSet(optGenerate))
        return generateCorpus(parser.value(optGenerate)) ? 0 : 1;

    if (parser.isSet(optRun)) {
        if (!parser.isSet(optModel)) {
            fprintf(stderr, "--run requires --model <model.gguf>\n");
            return 1;
        }
        return runEval(parser.value(optRun), parser.value(optModel), parser.value(optLimit).toInt());
    }

    parser.showHelp(1);
}

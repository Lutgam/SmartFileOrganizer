#include "LlamaEngine.h"
#include <QByteArray>
#include <QDebug>
#include <QtGlobal>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

// Helper to add token to batch
static void batch_add(llama_batch &batch, llama_token id, llama_pos pos,
                      const std::vector<llama_seq_id> &seq_ids, bool logits) {
  batch.token[batch.n_tokens] = id;
  batch.pos[batch.n_tokens] = pos;
  batch.n_seq_id[batch.n_tokens] = seq_ids.size();
  for (size_t i = 0; i < seq_ids.size(); ++i) {
    batch.seq_id[batch.n_tokens][i] = seq_ids[i];
  }
  batch.logits[batch.n_tokens] = logits;
  batch.n_tokens++;
}

/// Serializes all llama.cpp access (single shared ctx/model). Held for the full inference, not just a counter bump.
struct InferenceGuard {
    LlamaEngine *e = nullptr;
    explicit InferenceGuard(LlamaEngine *engine) : e(engine)
    {
        if (!e) return;
        e->stopIdleTimerAsync();
        e->m_mutex.lock();
        ++e->m_activeInferences;
    }
    ~InferenceGuard()
    {
        if (!e) return;
        --e->m_activeInferences;
        e->m_mutex.unlock();
        e->startIdleTimerAsync();
    }
};

LlamaEngine::LlamaEngine(QObject *parent) : QObject(parent) {
  llama_backend_init();

  idleTimer = new QTimer(this);
  idleTimer->setInterval(3 * 60 * 1000); // 3 minutes
  idleTimer->setSingleShot(true);
  connect(idleTimer, &QTimer::timeout, this, [this]() { unloadModel(); });
}

LlamaEngine::~LlamaEngine() {
  if (idleTimer) idleTimer->stop();
  unloadModel();
  llama_backend_free();
}

void LlamaEngine::setOutputLanguage(const QString &lang) {
  QMutexLocker<QRecursiveMutex> locker(&m_mutex);
  const QString cleaned = lang.trimmed();
  if (cleaned.isEmpty()) return;
  m_currentLanguage = cleaned;
}

void LlamaEngine::stopIdleTimerAsync() {
  if (!idleTimer) return;
  if (QThread::currentThread() == thread()) {
    idleTimer->stop();
    return;
  }
  QMetaObject::invokeMethod(this, [this]() {
    if (idleTimer) idleTimer->stop();
  }, Qt::QueuedConnection);
}

void LlamaEngine::startIdleTimerAsync() {
  if (!idleTimer) return;
  if (QThread::currentThread() == thread()) {
    idleTimer->start();
    return;
  }
  QMetaObject::invokeMethod(this, [this]() {
    if (idleTimer) idleTimer->start();
  }, Qt::QueuedConnection);
}

void LlamaEngine::unloadModel() {
  QMutexLocker<QRecursiveMutex> locker(&m_mutex);
  if (m_activeInferences > 0) {
    // Still in use; postpone unload.
    if (idleTimer) idleTimer->start();
    return;
  }

  if (ctx) {
    llama_free(ctx);
    ctx = nullptr;
  }
  if (model) {
    llama_model_free(model);
    model = nullptr;
  }
  qDebug() << "[系統] AI 模型閒置超時，已釋放記憶體";
}

bool LlamaEngine::ensureModelLoaded() {
  // Avoid holding the mutex while calling into Qt file APIs too long? We'll keep it simple and safe.
  QMutexLocker<QRecursiveMutex> locker(&m_mutex);
  if (model && ctx) return true;
  if (m_modelPath.empty()) return false;

  // Free any partial state
  if (ctx) {
    llama_free(ctx);
    ctx = nullptr;
  }
  if (model) {
    llama_model_free(model);
    model = nullptr;
  }

  QFileInfo fileInfo(QString::fromStdString(m_modelPath));
  QString absPath = fileInfo.absoluteFilePath();
  qDebug() << "嘗試載入模型，路徑：" << absPath;

  llama_model_params model_params = llama_model_default_params();
  model_params.n_gpu_layers = 100; // Try to use GPU
  model = llama_model_load_from_file(absPath.toStdString().c_str(), model_params);

  if (!model) {
    qDebug() << "模型載入失敗";
    std::cerr << "Failed to load model from " << absPath.toStdString() << std::endl;
    return false;
  }

  llama_context_params ctx_params = llama_context_default_params();
  ctx_params.n_ctx = 2048;
  // Default n_batch is often smaller than n_ctx; our decode path submits the full prompt in one batch.
  ctx_params.n_batch = std::max(ctx_params.n_batch, ctx_params.n_ctx);
  ctx = llama_init_from_model(model, ctx_params);

  if (!ctx) {
    qDebug() << "模型載入失敗 (Context Error)";
    std::cerr << "Failed to create context" << std::endl;
    llama_model_free(model);
    model = nullptr;
    return false;
  }

  qDebug() << "模型載入成功";
  return true;
}

bool LlamaEngine::isModelLoaded() const
{
  QMutexLocker<QRecursiveMutex> locker(&m_mutex);
  return model != nullptr;
}

bool LlamaEngine::loadModel(const std::string &modelPath)
{
  return loadModelImpl(modelPath);
}

bool LlamaEngine::loadModelImpl(const std::string &modelPath)
{
  const QFileInfo modelFile(QString::fromStdString(modelPath));
  if (!modelFile.exists() || !modelFile.isFile()) {
    qWarning() << "[AI] Model path missing or not a file:" << QString::fromStdString(modelPath);
    return false;
  }
  constexpr qint64 kMinModelBytes = 100LL * 1024 * 1024;
  if (modelFile.size() < kMinModelBytes) {
    qWarning() << "[AI] Model file too small (incomplete download?):" << modelFile.absoluteFilePath()
               << "bytes:" << modelFile.size();
    return false;
  }

  {
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    m_modelPath = modelPath;
  }

  stopIdleTimerAsync();
  const bool ok = ensureModelLoaded();
  if (ok) startIdleTimerAsync();
  return ok;
}

std::string LlamaEngine::generateResponse(const std::string &prompt, int maxNewTokens)
{
  const int cap = std::max(1, std::min(maxNewTokens, 32768));
  return generateResponseImpl(prompt, cap);
}

std::string LlamaEngine::generateResponseImpl(const std::string &prompt, int maxNewTokens)
{
  InferenceGuard guard(this);
  if (!ensureModelLoaded()) return "Error: Model not loaded";

  struct LlamaKvCacheFinalClear {
    llama_context *c;
    explicit LlamaKvCacheFinalClear(llama_context *ctx) : c(ctx) {}
    ~LlamaKvCacheFinalClear()
    {
#if defined(LLAMA_API_VERSION)
      if (c) llama_kv_cache_clear(c);
#endif
    }
  } kvFinalClear(ctx);

  // Clear context/KV cache to avoid cross-file leakage.
  // Prefer llama_kv_cache_clear when available; keep legacy memory clear as a fallback.
#if defined(LLAMA_API_VERSION)
  llama_kv_cache_clear(ctx);
#endif
  llama_memory_t mem = llama_get_memory(ctx);
  llama_memory_seq_rm(mem, -1, -1, -1);

  const llama_vocab *vocab = llama_model_get_vocab(model);
  if (!vocab) return "Error: vocabulary not available";

  // 1. Tokenize
  const int n_prompt_raw = -llama_tokenize(vocab, prompt.c_str(), prompt.length(),
                                           NULL, 0, true, false);
  if (n_prompt_raw <= 0 || n_prompt_raw > 100000)
    return "Error: Tokenization size";
  std::vector<llama_token> prompt_tokens(static_cast<size_t>(n_prompt_raw));
  if (llama_tokenize(vocab, prompt.c_str(), prompt.length(),
                     prompt_tokens.data(), n_prompt_raw, true, false) < 0) {
    return "Error: Tokenization failed";
  }

  const int n_ctx = static_cast<int>(llama_n_ctx(ctx));
  const int n_batch = static_cast<int>(llama_n_batch(ctx));
  constexpr int kReserveGenTokens = 200;
  const int maxByCtx = std::max(1, n_ctx - kReserveGenTokens);
  const int maxByBatch = std::max(1, n_batch - 8);
  const size_t maxPromptTokens = static_cast<size_t>(std::min(maxByCtx, maxByBatch));
  if (prompt_tokens.size() > maxPromptTokens) {
    qWarning() << "[AI] Prompt token count exceeds context window; refusing decode.";
    return "Error: Prompt length exceeds context window";
  }
  const int n_prompt = static_cast<int>(prompt_tokens.size());

  // 2. Initial Batch
  const int batchAlloc = std::max(n_prompt + 256, 512);
  llama_batch batch = llama_batch_init(batchAlloc, 0, 1);
  for (int i = 0; i < n_prompt; i++) {
    batch_add(batch, prompt_tokens[i], i, {0}, false);
  }
  batch.logits[batch.n_tokens - 1] = true;

  // 3. Decode
  if (llama_decode(ctx, batch) != 0) {
    llama_batch_free(batch);
    return "Error: llama_decode failed";
  }

  int n_curr = batch.n_tokens;
  llama_batch_free(batch);

  // 4. Sample loop (hard cap: always stop after maxNewTokens, even without EOS)
  std::stringstream response_ss;
  const int n_predict = std::max(1, std::min(maxNewTokens, 32768));

  auto sparams = llama_sampler_chain_default_params();
  struct llama_sampler *smpl = llama_sampler_chain_init(sparams);
  llama_sampler_chain_add(smpl, llama_sampler_init_greedy());

  llama_token new_token_id = 0;

  for (int i = 0; i < n_predict; ++i) {
    // Check cancel flag before each token
    if (m_cancelFlag && m_cancelFlag->load()) {
      break; // Abort inference gracefully
    }

    new_token_id = llama_sampler_sample(smpl, ctx, -1);

    if (llama_vocab_is_eog(vocab, new_token_id)) {
      break;
    }

    char buf[256];
    int n =
        llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf), 0, true);
    if (n >= 0) {
      std::string piece(buf, n);
      response_ss << piece;
    }

    llama_batch batch_one = llama_batch_init(1, 0, 1);
    batch_add(batch_one, new_token_id, n_curr, {0}, true);
    n_curr++;

    if (llama_decode(ctx, batch_one) != 0) {
      llama_batch_free(batch_one);
      break;
    }
    llama_batch_free(batch_one);
  }

  llama_sampler_free(smpl);

  return response_ss.str();
}

std::string LlamaEngine::suggestTags(const std::string &filename,
                                     const std::string &content,
                                     const std::string &rejectedTagsCsv,
                                     const std::string &existingTags,
                                     bool contentReadable,
                                     const std::string &fileExt)
{
  return suggestTagsImpl(filename, content, rejectedTagsCsv, existingTags, contentReadable, fileExt);
}

std::string LlamaEngine::suggestTagsImpl(const std::string &filename,
                                         const std::string &content,
                                         const std::string &rejectedTagsCsv,
                                         const std::string &existingTags,
                                         bool contentReadable,
                                         const std::string &fileExt)
{
  InferenceGuard guard(this);
  if (!ensureModelLoaded()) return "Error: Model not loaded";

  // --- Language awareness ---
  QString lang;
  {
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    lang = m_currentLanguage.trimmed();
  }
  const bool en = (lang.compare(QStringLiteral("en_US"), Qt::CaseInsensitive) == 0);

  // If content is not readable (binary), do NOT hallucinate based on raw bytes.
  if (!contentReadable) {
    const std::string fixedSummaryEn =
        "System cannot read the content of this file format. Classifying based on filename only.";
    const std::string fixedSummaryZh =
        "系統無法讀取此檔案格式的內容，僅依據檔名進行基礎分類。";

    std::string prompt;
    if (en) {
      prompt =
          "You are an expert file analyzer.\n"
          "You MUST output ONLY a valid JSON object. NO markdown. NO backticks. NO explanations.\n"
          "Output format: {\"summary\":\"...\",\"tags\":[\"...\",\"...\"]}\n"
          "\n"
          "The file content is NOT readable (binary).\n"
          "Filename: " +
          filename + "\n" + "File extension: " + fileExt +
          "\n\n"
          "REQUIREMENTS:\n"
          "1) The \"summary\" field MUST equal EXACTLY:\n"
          "\"" +
          fixedSummaryEn + "\"\n"
          "2) The \"tags\" field MUST contain at most 2 tags describing the file type/topic inferred ONLY from filename and extension.\n"
          "3) Tags must be concrete nouns or proper nouns. No long sentences.\n"
          "\nONLY output the JSON object.\n";
    } else {
      prompt =
          "你是專業的檔案分析助手。\n"
          "你必須只輸出一個有效的 JSON 物件。不要 markdown、不要反引號、不要任何解釋文字。\n"
          "輸出格式：{\"summary\":\"...\",\"tags\":[\"...\",\"...\"]}\n"
          "\n"
          "這是一個二進位檔案，內容無法讀取。\n"
          "檔名: " +
          filename + "\n" + "副檔名: " + fileExt +
          "\n\n"
          "要求：\n"
          "1) \"summary\" 欄位必須完全等於：「" +
          fixedSummaryZh + "」。\n"
          "2) \"tags\" 欄位最多給出 2 個描述檔案類型或主題的標籤（只能依據檔名與副檔名推測）。\n"
          "3) 標籤必須是名詞或專有名詞，不能是長句。\n"
          "\n只能輸出 JSON 物件本體。\n";
    }
    return generateResponseImpl(prompt, kMaxNewTokensSuggestTagsJson);
  }

  // Force structured JSON output (summary + tags) with strict rules
  std::string instruction;
  if (en) {
    instruction =
        "You are an expert file analyzer.\n"
        "You MUST output ONLY a valid JSON object. NO markdown. NO backticks. NO explanations.\n"
        "Output format: {\"summary\":\"...\",\"tags\":[\"...\",\"...\",\"...\"]}\n"
        "\n"
        "SUMMARY RULES:\n"
        "- Write EXACTLY one sentence describing the core fact or topic.\n"
        "- NEVER mention file format or type (e.g., do not say \"this is a document/file\").\n"
        "- Do NOT use the words: document, file, contains, includes.\n"
        "- NEVER describe structure (e.g., do not say \"it has 6 questions/sections\").\n"
        "- Prefer a topic statement like: \"SQL scripts for a university database (departments, instructors, students).\"\n"
        "\n"
        "TAG RULES:\n"
        "- Output at most 3 tags.\n"
        "- Each tag MUST be a concrete noun or proper noun.\n"
        "- Tags MUST NOT be long sentences.\n"
        "- CRITICAL: Tags MUST strictly describe the functional type of the document (e.g., 'Score Sheet', 'Exam Paper', 'Financial Report', 'Resume', 'Receipt', 'Database SQL').\n"
        "- DO NOT use broad environment tags (e.g., 'School', 'Work', 'Company').\n"
        "- ABSOLUTELY DO NOT generate specific institution or university names (e.g., 'TCUS', 'Ministry of Education') unless it is the absolute core subject of the file.\n"
        "- Tags MUST be in English.\n";
  } else {
    instruction =
        "你是專業的檔案分析助手。\n"
        "你必須只輸出一個有效的 JSON 物件。不要 markdown、不要反引號、不要任何解釋文字。\n"
        "輸出格式：{\"summary\":\"...\",\"tags\":[\"...\",\"...\",\"...\"]}\n"
        "\n"
        "摘要規則：\n"
        "- 必須只用一句話總結檔案的核心事實或主題。\n"
        "- 絕對不要提及檔案格式或類型（例如：不要說「這是一份文件/檔案」）。\n"
        "- 絕對不要使用「文件/檔案/包含/內容」等開頭句型。\n"
        "- 絕對不要描述結構（例如：不要說「總共有 6 個問題/章節」）。\n"
        "- 請直接用「主題句」描述，例如：「校務資料庫的 SQL 建表與資料填充腳本（系所、教師、學生）。」。\n"
        "\n"
        "標籤規則：\n"
        "- 最多只能輸出 3 個標籤。\n"
        "- 每個標籤必須是具體的名詞或專有名詞。\n"
        "- 標籤絕不能是長句子。\n"
        "【極度重要】：標籤必須精確描述檔案的「實體功能或格式類型」（例如：計分表、測驗卷、財務報表、履歷）。\n"
        "絕對禁止使用廣泛的場域標籤（如：學校、工作、公司），也【絕對禁止】擅自生成特定的機構、大學或專有名詞（如：台綜大、台大、教育部），除非該名詞是檔案的絕對核心主題。\n"
        "- summary 與 tags 必須使用繁體中文。\n";
  }

  std::string prompt;
  if (content.empty()) {
    prompt = instruction + (en ? "\nFilename: " : "\n檔名: ") + filename + (en ? "\nOutput JSON:" : "\n請輸出 JSON：");
  } else {
    // Hard cap prompt size: first 1000 QString characters (not raw bytes) for stable UTF-8 handling.
    const QString qContent =
        QString::fromUtf8(QByteArray::fromRawData(content.data(), static_cast<int>(content.size())));
    const QByteArray snippetUtf8 = qContent.left(1000).toUtf8();
    const std::string safeContent(snippetUtf8.constData(), static_cast<size_t>(snippetUtf8.size()));
    prompt = instruction + (en ? "\nFilename: " : "\n檔名: ") + filename +
             (en ? "\nContent snippet: " : "\n內容片段: ") + safeContent + (en ? "\nOutput JSON:" : "\n請輸出 JSON：");
  }

  if (!rejectedTagsCsv.empty()) {
    prompt += en ? "\nSTRICT: Do NOT use the following tags: " + rejectedTagsCsv + "\n"
                 : "\n【嚴格限制】：請絕對不要使用以下標籤進行分類：" + rejectedTagsCsv + "\n";
  }

  if (!existingTags.empty()) {
    prompt += en ? "\nExisting tags in library (avoid duplicates if possible): " + existingTags + "\n"
                 : "\n系統既有標籤（盡量避免重複）： " + existingTags + "\n";
  }

  // Final hard reminder: JSON only.
  prompt += en ? "\nONLY output the JSON object. NO markdown. NO backticks. NO extra text.\n"
              : "\n只能輸出 JSON 物件本體，不要 markdown，不要反引號，不要任何額外文字。\n";

  // Return raw model output (JSON expected). C++ side will sanitize/parse with fallback.
  return generateResponseImpl(prompt, kMaxNewTokensSuggestTagsJson);
}


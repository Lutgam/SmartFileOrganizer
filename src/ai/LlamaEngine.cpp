#include "LlamaEngine.h"
#include "../core/TagManager.h"
#include "../gui/LanguageManager.h"
#include <QByteArray>
#include <QDebug>
#include <QSettings>
#include <QThread>
#include <QtGlobal>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

namespace {
/// QSettings key shared with SettingsPanel (`performance/cpu_threads`).
int readLlamaCpuThreadsFromSettings()
{
    QSettings settings;
    const int ideal = qMax(1, QThread::idealThreadCount());
    const int defaultThreads = qMax(1, ideal / 2);
    int targetThreads = settings.value(QStringLiteral("performance/cpu_threads"), defaultThreads).toInt();
    return qBound(1, targetThreads, ideal);
}
} // namespace

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
  ctx_params.n_ctx = 8192;
  ctx_params.n_batch = 2048;
  {
      const int nThreads = readLlamaCpuThreadsFromSettings();
      ctx_params.n_threads = nThreads;
      ctx_params.n_threads_batch = nThreads;
  }
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
  if (m_cancelFlag && m_cancelFlag->load(std::memory_order_acquire)) {
    return "Error: Cancelled";
  }

  {
      const int nThreads = readLlamaCpuThreadsFromSettings();
      llama_set_n_threads(ctx, nThreads, nThreads);
  }

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
    if (m_cancelFlag && m_cancelFlag->load(std::memory_order_acquire)) {
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

namespace {
constexpr const char kJsonOnlyStrictEn[] =
    "You MUST output ONLY a valid JSON object. DO NOT output any other text, no explanations, "
    "no markdown formatting. Start your response with '{' and end with '}'.\n";

constexpr const char kSuggestTagsFallbackJson[] =
    R"({"summary":"[系統提示：檔案內容過於複雜或包含特殊編碼，已切換至安全模式讀取檔名進行智能分類。]","tags":["通用文件"]})";

QString analysisNonstandardFormatFallbackJson()
{
  auto &lm = LanguageManager::instance();
  QJsonObject o;
  o.insert(QStringLiteral("summary"), lm.getText(QStringLiteral("analysis_nonstandard_format_summary")));
  QJsonArray ta;
  ta.append(lm.getText(QStringLiteral("analysis_manual_classify_tag")));
  o.insert(QStringLiteral("tags"), ta);
  return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString analysisParsedNoSummaryText()
{
  return LanguageManager::instance().getText(QStringLiteral("analysis_parsed_no_summary"));
}

std::string suggestTagsFailsafeJson()
{
  const QString fallbackJson = QString::fromUtf8(kSuggestTagsFallbackJson);
  const QJsonDocument doc = QJsonDocument::fromJson(fallbackJson.toUtf8());
  return doc.toJson(QJsonDocument::Compact).toStdString();
}

QString stripMarkdownJsonFences(QString text)
{
  text = text.trimmed();
  text.remove(QRegularExpression(QStringLiteral("^```\\s*json\\s*"), QRegularExpression::CaseInsensitiveOption));
  text.remove(QRegularExpression(QStringLiteral("^```\\s*")));
  text.remove(QRegularExpression(QStringLiteral("```\\s*$")));
  return text.trimmed();
}

bool suggestTagsJsonObjectValid(const QJsonObject &obj)
{
  const QString summary = obj.value(QStringLiteral("summary")).toString().trimmed();
  if (summary.isEmpty()) return false;
  const QJsonValue tagsV = obj.value(QStringLiteral("tags"));
  if (!tagsV.isArray()) return false;
  for (const QJsonValue &v : tagsV.toArray()) {
    if (!v.toString().trimmed().isEmpty()) return true;
  }
  return false;
}

/// Brutal slice: first `{` through last `}`; if none, return compact fallback JSON for Qt to parse.
QString brutalExtractJsonFromLlmResponse(QString text)
{
  text = stripMarkdownJsonFences(text.trimmed());
  const int startIndex = text.indexOf(QLatin1Char('{'));
  const int endIndex = text.lastIndexOf(QLatin1Char('}'));
  if (startIndex != -1 && endIndex != -1 && endIndex >= startIndex)
    return text.mid(startIndex, endIndex - startIndex + 1);
  return analysisNonstandardFormatFallbackJson();
}

QString extractOutermostJsonObject(QString text)
{
  return brutalExtractJsonFromLlmResponse(text);
}

QString aiTagDedupKey(QString tag)
{
  tag = tag.trimmed().toLower();
  static const QRegularExpression noiseSuffix(
      QStringLiteral("(檔案|文件|內容|設定檔|設定|變數|配置|資料)+$"));
  tag.remove(noiseSuffix);
  tag.remove(QRegularExpression(QStringLiteral("\\.(txt|json|xml|yaml|yml|ini|cfg|conf)$"),
                               QRegularExpression::CaseInsensitiveOption));
  return tag.trimmed();
}

QStringList parseRejectedTagsCsv(const std::string &rejectedTagsCsv)
{
  QStringList out;
  for (QString part : QString::fromStdString(rejectedTagsCsv).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    part = TagManager::stripAiPrefix(part.trimmed());
    if (!part.isEmpty()) out << part;
  }
  return out;
}

bool isTagOnRejectedList(const QString &tag, const QStringList &rejectedTags)
{
  if (rejectedTags.isEmpty()) return false;
  const QString norm = TagManager::stripAiPrefix(tag).trimmed();
  if (norm.isEmpty()) return false;
  const QString key = aiTagDedupKey(norm);
  for (const QString &rejected : rejectedTags) {
    const QString rejNorm = TagManager::stripAiPrefix(rejected).trimmed();
    if (rejNorm.isEmpty()) continue;
    if (norm.compare(rejNorm, Qt::CaseInsensitive) == 0) return true;
    const QString rejKey = aiTagDedupKey(rejNorm);
    if (!key.isEmpty() && !rejKey.isEmpty()
        && (key == rejKey || key.contains(rejKey) || rejKey.contains(key))) {
      return true;
    }
  }
  return false;
}

QJsonArray sanitizeTagsArray(const QJsonArray &arr, const QStringList &rejectedTags = {},
                             int maxTags = 3, int maxLen = 15)
{
  QJsonArray out;
  QSet<QString> seenKeys;
  for (const QJsonValue &v : arr) {
    if (out.size() >= maxTags) break;

    QString t = v.toString().trimmed();
    if (t.isEmpty() || t.compare(QStringLiteral("ai"), Qt::CaseInsensitive) == 0)
      continue;
    if (isTagOnRejectedList(t, rejectedTags))
      continue;
    if (t.size() > maxLen)
      continue;

    const QString key = aiTagDedupKey(t);
    if (key.isEmpty() || seenKeys.contains(key))
      continue;

    bool overlaps = false;
    for (int i = 0; i < out.size(); ++i) {
      const QString existingKey = aiTagDedupKey(out.at(i).toString());
      if (existingKey.isEmpty())
        continue;
      if (key == existingKey || key.contains(existingKey) || existingKey.contains(key)) {
        overlaps = true;
        break;
      }
    }
    if (overlaps)
      continue;

    seenKeys.insert(key);
    out.append(t);
  }

  if (out.isEmpty())
    out.append(LanguageManager::instance().getText(QStringLiteral("analysis_manual_classify_tag")));
  return out;
}

QString coerceSummaryText(QString summary)
{
  summary = summary.trimmed();
  if (summary.isEmpty() || summary == QStringLiteral("..."))
    return analysisParsedNoSummaryText();

  if (summary.startsWith(QLatin1Char('{')) || summary.contains(QStringLiteral("\"summary\""))) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(summary.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
      const QString inner = doc.object().value(QStringLiteral("summary")).toString().trimmed();
      if (!inner.isEmpty())
        summary = inner;
    }
  }

  if (summary.size() > 500)
    summary = summary.left(500).trimmed();
  if (summary.isEmpty() || summary == QStringLiteral("..."))
    summary = analysisParsedNoSummaryText();
  return summary;
}

QJsonObject sanitizeSuggestTagsObject(const QJsonObject &obj, const QStringList &rejectedTags)
{
  QJsonObject out;
  QString summary = coerceSummaryText(obj.value(QStringLiteral("summary")).toString());
  if (summary.isEmpty() || summary == QStringLiteral("..."))
    summary = analysisParsedNoSummaryText();
  out.insert(QStringLiteral("summary"), summary);
  out.insert(QStringLiteral("tags"), sanitizeTagsArray(obj.value(QStringLiteral("tags")).toArray(), rejectedTags));
  return out;
}

std::string jsonFromUnblockedRawOutput(QString cleanOutput)
{
  cleanOutput = stripMarkdownJsonFences(cleanOutput).trimmed();
  if (cleanOutput.isEmpty()) {
    cleanOutput = QStringLiteral(
        "[系統提示：檔案內容過於複雜或包含特殊編碼，已切換至安全模式讀取檔名進行智能分類。]");
  }

  QJsonObject fallbackObj;
  fallbackObj.insert(QStringLiteral("summary"), cleanOutput);
  QJsonArray fallbackTags;
  fallbackTags.append(QStringLiteral("智能解析摘要"));
  fallbackObj.insert(QStringLiteral("tags"), fallbackTags);

  qDebug() << "=== AI Raw Output Unblocked ===";
  qDebug() << cleanOutput;

  return QJsonDocument(fallbackObj).toJson(QJsonDocument::Compact).toStdString();
}

std::string wrapRawLlmOutputAsSuggestTagsJson(const QString &rawResponse)
{
  return jsonFromUnblockedRawOutput(rawResponse);
}

std::string ensureSuggestTagsJson(const std::string &raw, const std::string &rejectedTagsCsv)
{
  const QStringList rejectedTags = parseRejectedTagsCsv(rejectedTagsCsv);
  const QString llmOutputString = QString::fromStdString(raw).trimmed();
  if (llmOutputString.isEmpty())
    return suggestTagsFailsafeJson();
  if (llmOutputString.startsWith(QStringLiteral("Error:"), Qt::CaseInsensitive)) {
    if (llmOutputString.startsWith(QStringLiteral("Error: Cancelled"), Qt::CaseInsensitive))
      return raw;
    return suggestTagsFailsafeJson();
  }

  QString rawResponse = brutalExtractJsonFromLlmResponse(llmOutputString);

  QJsonParseError parseError{};
  QJsonDocument doc = QJsonDocument::fromJson(rawResponse.toUtf8(), &parseError);

  if (parseError.error == QJsonParseError::NoError && doc.isObject()
      && doc.object().contains(QStringLiteral("summary"))) {
    return QJsonDocument(sanitizeSuggestTagsObject(doc.object(), rejectedTags))
        .toJson(QJsonDocument::Compact)
        .toStdString();
  }

  static const QRegularExpression re(QStringLiteral("\\{.*?\\}"));
  QRegularExpression jsonBlockRe(re);
  jsonBlockRe.setPatternOptions(QRegularExpression::DotMatchesEverythingOption);
  const QString stripped = stripMarkdownJsonFences(llmOutputString);
  auto it = jsonBlockRe.globalMatch(stripped);
  while (it.hasNext()) {
    const QString block = extractOutermostJsonObject(it.next().captured(0).trimmed());
    if (block.isEmpty())
      continue;
    QJsonParseError blockError{};
    const QJsonDocument blockDoc = QJsonDocument::fromJson(block.toUtf8(), &blockError);
    if (blockError.error != QJsonParseError::NoError || !blockDoc.isObject())
      continue;
    if (!blockDoc.object().contains(QStringLiteral("summary")))
      continue;
    return QJsonDocument(sanitizeSuggestTagsObject(blockDoc.object(), rejectedTags))
        .toJson(QJsonDocument::Compact)
        .toStdString();
  }

  return jsonFromUnblockedRawOutput(stripped);
}
} // namespace

std::string LlamaEngine::suggestTags(const std::string &filename,
                                     const std::string &content,
                                     const std::string &rejectedTagsCsv,
                                     const std::string &existingTags,
                                     bool contentReadable,
                                     const std::string &fileExt,
                                     bool pdfMetadataOnly)
{
  return ensureSuggestTagsJson(
      suggestTagsImpl(filename, content, rejectedTagsCsv, existingTags, contentReadable, fileExt, pdfMetadataOnly),
      rejectedTagsCsv);
}

std::string LlamaEngine::suggestTagsImpl(const std::string &filename,
                                         const std::string &content,
                                         const std::string &rejectedTagsCsv,
                                         const std::string &existingTags,
                                         bool contentReadable,
                                         const std::string &fileExt,
                                         bool pdfMetadataOnly)
{
  InferenceGuard guard(this);
  if (!ensureModelLoaded()) return "Error: Model not loaded";

#if defined(LLAMA_API_VERSION)
  if (ctx) llama_kv_cache_clear(ctx);
#endif

  // --- Language awareness ---
  QString lang;
  {
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    lang = m_currentLanguage.trimmed();
  }
  const bool en = (lang.compare(QStringLiteral("en_US"), Qt::CaseInsensitive) == 0);
  const QString contentQ = QString::fromStdString(content).trimmed();
  const bool treatContentReadable = contentReadable || contentQ.size() > 10;

  if (pdfMetadataOnly) {
    const std::string fixedSummaryEn =
        "Read filename and attributes for intelligent classification.";
    const std::string fixedSummaryZh = "已讀取 Metadata 進行智能分類。";

    std::string prompt;
    if (en) {
      prompt =
          "You are an expert file analyzer.\n"
          "You MUST output ONLY a valid JSON object. NO markdown. NO backticks. NO explanations.\n" +
          std::string(kJsonOnlyStrictEn) +
          "Output format: {\"summary\":\"...\",\"tags\":[\"...\",\"...\"]}\n"
          "\n"
          "This file has no reliable extractable text. Use filename and attributes only.\n"
          "Filename: " +
          filename + "\n" + "File extension: " + fileExt + "\n" + "Context: " + content +
          "\n\n"
          "REQUIREMENTS:\n"
          "1) The \"summary\" field MUST equal EXACTLY:\n"
          "\"" +
          fixedSummaryEn + "\"\n"
          "2) The \"tags\" field MUST contain 2 to 3 tags inferred ONLY from filename and extension.\n"
          "3) Tags must be concrete nouns or proper nouns. No long sentences.\n"
          "4) NEVER use generic words such as file, document, data, or content.\n"
          "\nONLY output the JSON object.\n";
    } else {
      prompt =
          "你是專業的檔案分析助手。\n"
          "你必須只輸出一個有效的 JSON 物件。不要 markdown、不要反引號、不要任何解釋文字。\n"
          "輸出格式：{\"summary\":\"...\",\"tags\":[\"...\",\"...\"]}\n"
          "\n"
          "此檔案無法抽取可靠內文，僅能依檔名與屬性分類。\n"
          "檔名: " +
          filename + "\n" + "副檔名: " + fileExt + "\n" + "屬性: " + content +
          "\n\n"
          "要求：\n"
          "1) \"summary\" 欄位必須完全等於：「" +
          fixedSummaryZh + "」。\n"
          "2) \"tags\" 欄位必須給出 1 到 3 個標籤（最多 3 個；只能依據檔名與副檔名推測）。\n"
          "3) 標籤必須是名詞或專有名詞，不能是長句。\n"
          "4) 嚴禁使用「檔案、文件、資料、內容」等無意義通用詞。\n"
          "\n只能輸出 JSON 物件本體。\n";
    }
    return generateResponseImpl(prompt, kMaxNewTokensSuggestTagsJson);
  }

  // If content is not readable (binary), do NOT hallucinate based on raw bytes.
  if (!treatContentReadable) {
    const std::string fixedSummaryEn =
        "System cannot read the content of this file format. Classifying based on filename only.";
    const std::string fixedSummaryZh =
        "系統無法讀取此檔案格式的內容，僅依據檔名進行基礎分類。";

    std::string prompt;
    if (en) {
      prompt =
          "You are an expert file analyzer.\n"
          "You MUST output ONLY a valid JSON object. NO markdown. NO backticks. NO explanations.\n" +
          std::string(kJsonOnlyStrictEn) +
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
          "2) The \"tags\" field MUST contain 1 to 3 tags (max 3) describing the file type/topic inferred ONLY from filename and extension.\n"
          "3) Tags must be concrete nouns or proper nouns. No long sentences.\n"
          "4) NEVER use generic words such as file, document, data, or content.\n"
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
          "2) \"tags\" 欄位必須給出 1 到 3 個標籤（最多 3 個；只能依據檔名與副檔名推測）。\n"
          "3) 標籤必須是名詞或專有名詞，不能是長句。\n"
          "4) 嚴禁使用「檔案、文件、資料、內容」等無意義通用詞。\n"
          "\n只能輸出 JSON 物件本體。\n";
    }
    return generateResponseImpl(prompt, kMaxNewTokensSuggestTagsJson);
  }

  // Force structured JSON output (summary + tags) with strict rules
  std::string instruction;
  if (en) {
    instruction =
        "You are an expert file analyzer.\n"
        "You MUST output ONLY a valid JSON object. NO markdown. NO backticks. NO explanations.\n" +
        std::string(kJsonOnlyStrictEn) +
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
        "- Output limit: generate exactly 1 to 3 tags (max 3).\n"
        "- Each tag MUST combine topic/subject AND document-type signal (e.g., year, project name, technology keyword, document nature).\n"
        "- Prefer concrete signals: years (e.g., 2024), project names, technologies (Python, SQL), and document kinds (report, invoice, notes, exam paper).\n"
        "- Each tag MUST be a short concrete noun or proper noun (no long sentences).\n"
        "- NEVER use meaningless generic words such as: file, document, data, material, content, info, item.\n"
        "- DO NOT use broad environment-only tags (school, work, company) without a specific topic.\n"
        "- DO NOT invent institution names unless they are the core subject.\n"
        "- Example tags for a database exam: [\"2024\", \"database\", \"exam paper\", \"SQL\"].\n"
        "- Tags MUST be in English.\n";
  } else {
    instruction =
        "你是一個精準的檔案分類代理。請閱讀檔案摘要，嚴格提取 1 到 3 個具有實質業務意義的關鍵字標籤（最多 3 個）。\n"
        "你必須只輸出一個有效的 JSON 物件。不要 markdown、不要反引號、不要任何解釋文字。\n"
        "輸出格式：{\"summary\":\"...\",\"tags\":[\"...\",\"...\",\"...\"]}\n"
        "\n"
        "絕對禁止使用：'檔案', '資料', '文件', '內容' 等無意義詞彙。\n"
        "請學習以下多種角色與領域的分類風格：\n"
        "- [理工] 檔名「112演算法期末解答.pdf」 -> tags 類似：演算法, 期末考, 解答, 資訊工程, 大學課程\n"
        "- [醫學] 檔名「解剖學大體實驗講義_ch3.pdf」 -> tags 類似：解剖學, 實驗講義, 醫學, 生理\n"
        "- [法商] 檔名「民法總則筆記_期中重點.docx」 -> tags 類似：民法, 法律, 課程筆記, 考試重點\n"
        "- [語言] 檔名「托福單字大全_分類版.pdf」 -> tags 類似：托福, TOEFL, 英文單字, 語言檢定\n"
        "- [底層] 檔名「kernel_driver_config.h」 -> tags 類似：C語言, Linux, 驅動程式, 系統開發\n"
        "- [網頁] 檔名「user_authentication.ts」 -> tags 類似：TypeScript, 會員認證, 後端開發, 網頁程式\n"
        "- [AI] 檔名「finetune_llama3_lora.py」 -> tags 類似：Llama3, 機器學習, 模型微調, Python\n"
        "- [生活] 檔名「長榮航空_台北_東京_電子機票.pdf」 -> tags 類似：電子機票, 東京, 長榮航空, 旅遊票證\n"
        "- [設計] 檔名「app_home_screen_mockup.fig」 -> tags 類似：Figma, UI設計, 介面草圖, 創意素材\n"
        "- [職場] 檔名「2024Q3_行銷企劃案_v2.pptx」 -> tags 類似：行銷企劃, 簡報, 商業提案, 職場管理\n"
        "- [財務] 檔名「2023年度個人綜合所得稅申報表.pdf」 -> tags 類似：所得稅, 報稅, 財務報表, 會計\n"
        "- [暫存] 檔名「~$碩士論文_最終版_絕對不改v8.docx」 -> tags 類似：暫存, 論文備份, 草稿, Word\n"
        "- [行動] 檔名「MainActivity.kt」 -> tags 類似：Kotlin, Android, 行動開發, 應用程式\n"
        "- [人文] 檔名「近代歐洲史_期末報告.docx」 -> tags 類似：歐洲史, 歷史, 期末報告, 人文學科\n"
        "- [數據] 檔名「sales_dashboard_2024Q4.xlsx」 -> tags 類似：Excel, 儀表板, 銷售分析, 財務報表\n"
        "- [弱語意-暫存] 檔名「report_final_v3.docx」 -> tags 類似：暫存, 草稿, Word文件\n"
        "- [弱語意-掃描] 檔名「掃描0042.jpg」 -> tags 類似：掃描圖片, 暫存\n"
        "- [弱語意-截圖] 檔名「Screenshot_2024.png」 -> tags 類似：截圖, 暫存\n"
        "- [弱語意-備份] 檔名「backup_2024.zip」 -> tags 類似：備份壓縮, 系統備份\n"
        "- [弱語意-多義] 檔名「data.csv」 -> tags 類似：數據, 暫存\n"
        "若檔名與內容資訊不足，只能輸出保守、可驗證的標籤；禁止臆測主題、組織或領域。\n"
        "請依此精準度與領域極端多樣性產出 tags；summary 與 tags 必須使用繁體中文。\n"
        "\n"
        "摘要規則：\n"
        "- 必須只用一句話總結檔案的核心事實或主題。\n"
        "- 絕對不要提及檔案格式或類型（例如：不要說「這是一份文件/檔案」）。\n"
        "- 絕對不要使用「文件/檔案/包含/內容」等開頭句型。\n"
        "- 絕對不要描述結構（例如：不要說「總共有 6 個問題/章節」）。\n";
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
    prompt += en ? "\nOutput limit: Generate exactly 1 to 3 tags (max 3). Must exclude the following tags: "
                       + rejectedTagsCsv + "\n"
                 : "\n輸出限制：請產生 1 到 3 個標籤（最多 3 個）。請排除以下標籤：" + rejectedTagsCsv + "\n";
  } else {
    prompt += en ? "\nOutput limit: Generate exactly 1 to 3 tags (max 3).\n"
                 : "\n輸出限制：請產生 1 到 3 個標籤（最多 3 個）。\n";
  }

  if (!existingTags.empty()) {
    prompt += en ? "\nExisting tags in library (avoid duplicates if possible): " + existingTags + "\n"
                 : "\n系統既有標籤（盡量避免重複）： " + existingTags + "\n";
  }

  // Final hard reminder: JSON only.
  prompt += kJsonOnlyStrictEn;
  prompt += en ? "\nONLY output the JSON object. NO markdown. NO backticks. NO extra text.\n"
              : "\n只能輸出 JSON 物件本體，不要 markdown，不要反引號，不要任何額外文字。\n"
                "回應必須以 '{' 開頭、以 '}' 結尾。\n";

  // Return raw model output (JSON expected). C++ side will sanitize/parse with fallback.
  return generateResponseImpl(prompt, kMaxNewTokensSuggestTagsJson);
}


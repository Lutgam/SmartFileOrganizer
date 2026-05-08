#include "LlamaEngine.h"
#include <QDebug>
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

struct InferenceGuard {
  LlamaEngine *e = nullptr;
  explicit InferenceGuard(LlamaEngine *engine) : e(engine) {
    if (!e) return;
    e->stopIdleTimerAsync();
    QMutexLocker locker(&e->m_mutex);
    ++e->m_activeInferences;
  }
  ~InferenceGuard() {
    if (!e) return;
    {
      QMutexLocker locker(&e->m_mutex);
      --e->m_activeInferences;
    }
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
  QMutexLocker locker(&m_mutex);
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
  QMutexLocker locker(&m_mutex);
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
  QMutexLocker locker(&m_mutex);
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

bool LlamaEngine::loadModel(const std::string &modelPath) {
  {
    QMutexLocker locker(&m_mutex);
    m_modelPath = modelPath;
  }

  stopIdleTimerAsync();
  const bool ok = ensureModelLoaded();
  if (ok) startIdleTimerAsync();
  return ok;
}

std::string LlamaEngine::generateResponse(const std::string &prompt) {
  InferenceGuard guard(this);
  if (!ensureModelLoaded()) return "Error: Model not loaded";

  // Clear KV cache
  llama_memory_t mem = llama_get_memory(ctx);
  llama_memory_seq_rm(mem, -1, -1, -1);

  const llama_vocab *vocab = llama_model_get_vocab(model);

  // 1. Tokenize
  const int n_prompt = -llama_tokenize(vocab, prompt.c_str(), prompt.length(),
                                       NULL, 0, true, false);
  std::vector<llama_token> prompt_tokens(n_prompt);
  if (llama_tokenize(vocab, prompt.c_str(), prompt.length(),
                     prompt_tokens.data(), n_prompt, true, false) < 0) {
    return "Error: Tokenization failed";
  }

  // 2. Initial Batch
  llama_batch batch = llama_batch_init(2048, 0, 1);
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

  // 4. Sample loop
  std::stringstream response_ss;
  int n_predict = 128;

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
                                     const std::string &existingTags) {
  InferenceGuard guard(this);
  if (!ensureModelLoaded()) return "Error: Model not loaded";

  // --- Language awareness ---
  QString lang;
  {
    QMutexLocker locker(&m_mutex);
    lang = m_currentLanguage.trimmed();
  }
  const bool en = (lang.compare(QStringLiteral("en_US"), Qt::CaseInsensitive) == 0);

  // Minimal, non-threatening prompt — avoids parroting
  std::string instruction = en
                                ? "Output two short topic tags for this file, separated by a comma. Output tags only."
                                : "請輸出兩個描述此檔案主題的詞彙，用逗號分隔，不要有任何其他文字。";

  std::string prompt;
  if (content.empty()) {
    prompt = instruction + (en ? "\nFilename: " : "\n檔名: ") + filename + (en ? "\nOutput:" : "\n輸出:");
  } else {
    std::string safeContent = content.substr(0, 600);
    prompt = instruction + (en ? "\nFilename: " : "\n檔名: ") + filename +
             (en ? "\nContent snippet: " : "\n內容片段: ") + safeContent + (en ? "\nOutput:" : "\n輸出:");
  }

  if (!rejectedTagsCsv.empty()) {
    prompt += en ? "\nSTRICT: Do NOT use the following tags: " + rejectedTagsCsv + "\n"
                 : "\n【嚴格限制】：請絕對不要使用以下標籤進行分類：" + rejectedTagsCsv + "\n";
  }

  prompt += en ? "\nIMPORTANT: The user interface is in English. You MUST output all tags, categories, and summaries entirely in English.\n"
              : "\n請使用繁體中文輸出標籤與摘要。\n";

  std::string rawResponse = generateResponse(prompt);

  // --- C++ 端物理字串清洗 (Hardcoded Sanitization) ---
  QString qRaw = QString::fromStdString(rawResponse);

  // 0. 拔除常見前綴詞
  qRaw.replace(QRegularExpression("System:", QRegularExpression::CaseInsensitiveOption), "");
  qRaw.replace("標籤:", "").replace("標签:", "");
  qRaw.replace("Assistant:", "").replace("User:", "").replace("輸出:", "");

  if (en) {
    // English: keep spaces, split by commas/newlines.
    qRaw.replace("\r", " ");
    QStringList parts = qRaw.split(QRegularExpression("[,\\n]"), Qt::SkipEmptyParts);
    QStringList out;
    for (const QString &p : parts) {
      QString t = p.trimmed();
      t.replace(QRegularExpression("^[-•\\s]+"), "");
      t.replace(QRegularExpression("[\"'`]+"), "");
      if (t.isEmpty()) continue;
      if (t.size() > 24) continue;
      out << t;
    }
    out.removeDuplicates();
    while (out.size() > 2) out.removeLast();
    return out.join(", ").toStdString();
  }

  // zh_TW: legacy cleanup
  qRaw.replace("。", "").replace(".", "").replace("\n", "").replace(" ", "");
  QStringList parts = qRaw.split(QRegularExpression("[,，、]"), Qt::SkipEmptyParts);
  QStringList blacklist = {"文件", "圖片", "音訊", "影片", "壓縮檔", "專案"};
  QStringList resultParts;
  for (const QString &p : parts) {
    QString t = p.trimmed();
    if (t.isEmpty()) continue;
    if (t.size() > 8) continue;
    if (blacklist.contains(t)) continue;
    resultParts << t;
  }
  resultParts.removeDuplicates();
  while (resultParts.size() > 2) resultParts.removeLast();
  return resultParts.join(", ").toStdString();
}


#include "LlamaEngine.h"
#include <QDebug>
#include <QFileInfo>
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

LlamaEngine::LlamaEngine() { llama_backend_init(); }

LlamaEngine::~LlamaEngine() {
  if (ctx)
    llama_free(ctx);
  if (model)
    llama_model_free(model);
  llama_backend_free();
}

bool LlamaEngine::loadModel(const std::string &modelPath) {
  if (model) {
    llama_model_free(model);
    model = nullptr;
  }
  if (ctx) {
    llama_free(ctx);
    ctx = nullptr;
  }

  QFileInfo fileInfo(QString::fromStdString(modelPath));
  QString absPath = fileInfo.absoluteFilePath();
  qDebug() << "嘗試載入模型，路徑：" << absPath;

  llama_model_params model_params = llama_model_default_params();
  model_params.n_gpu_layers = 100; // Try to use GPU
  model =
      llama_model_load_from_file(absPath.toStdString().c_str(), model_params);

  if (!model) {
    qDebug() << "模型載入失敗";
    std::cerr << "Failed to load model from " << absPath.toStdString()
              << std::endl;
    return false;
  }

  llama_context_params ctx_params = llama_context_default_params();
  ctx_params.n_ctx = 2048;
  ctx = llama_init_from_model(model, ctx_params);

  if (!ctx) {
    qDebug() << "模型載入失敗 (Context Error)";
    std::cerr << "Failed to create context" << std::endl;
    return false;
  }

  qDebug() << "模型載入成功";
  return true;
}

std::string LlamaEngine::generateResponse(const std::string &prompt) {
  if (!ctx || !model)
    return "Error: Model not loaded";

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
                                     const std::string &existingTags) {
  // Minimal, non-threatening prompt — avoids parroting
  std::string instruction =
      "請輸出兩個描述此檔案主題的詞彙，用逗號分隔，不要有任何其他文字。";

  std::string prompt;
  if (content.empty()) {
    prompt = instruction + "\n檔名: " + filename + "\n輸出:";
  } else {
    std::string safeContent = content.substr(0, 600);
    prompt = instruction + "\n檔名: " + filename +
             "\n內容片段: " + safeContent + "\n輸出:";
  }

  std::string rawResponse = generateResponse(prompt);

  // --- C++ 端物理字串清洗 (Hardcoded Sanitization) ---
  QString qRaw = QString::fromStdString(rawResponse);

  // 0. 拔除常見前綴詞
  qRaw.replace(QRegularExpression("System:", QRegularExpression::CaseInsensitiveOption), "");
  qRaw.replace("標籤:", "").replace("標签:", "");
  qRaw.replace("Assistant:", "").replace("User:", "").replace("輸出:", "");

  // 1. 移除點、句號、空白與換行
  qRaw.replace("。", "").replace(".", "").replace("\n", "").replace(" ", "");

  // 2. 以逗號與頓號切分
  QStringList parts = qRaw.split(QRegularExpression("[,，、]"), Qt::SkipEmptyParts);

  // 3. 物理長度過濾 (>8字元 = 幻覺/說明文字，直接丟棄) + 黑名單過濾
  QStringList blacklist = {"文件", "圖片", "音訊", "影片", "壓縮檔", "專案"};
  QStringList resultParts;
  for (const QString& p : parts) {
      QString t = p.trimmed();
      if (t.isEmpty()) continue;
      // Physical length gate: drop anything over 8 chars (stops hallucinated sentences)
      if (t.size() > 8) continue;
      // Blacklist gate
      if (blacklist.contains(t)) continue;
      resultParts << t;
  }

  // 4. 去重與截斷 (最多 2 個)
  resultParts.removeDuplicates();
  while (resultParts.size() > 2) {
      resultParts.removeLast();
  }

  // 5. 組合回傳
  return resultParts.join(", ").toStdString();
}


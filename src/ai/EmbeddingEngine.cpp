#include "EmbeddingEngine.h"

#include "llama.h"

#include <QDebug>
#include <QFileInfo>
#include <QMutexLocker>

#include <cmath>
#include <vector>

EmbeddingEngine::~EmbeddingEngine()
{
    unload();
}

void EmbeddingEngine::unload()
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (m_ctx) {
        llama_free(m_ctx);
        m_ctx = nullptr;
    }
    if (m_model) {
        llama_model_free(m_model);
        m_model = nullptr;
    }
    m_dim = 0;
}

bool EmbeddingEngine::loadModel(const std::string &modelPath)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    const QFileInfo fi(QString::fromStdString(modelPath));
    if (!fi.exists() || !fi.isFile()) {
        qWarning() << "[Embedding] model missing:" << fi.absoluteFilePath();
        return false;
    }
    unload();

    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = 100;
    m_model = llama_model_load_from_file(fi.absoluteFilePath().toStdString().c_str(), mp);
    if (!m_model) {
        qWarning() << "[Embedding] model load failed";
        return false;
    }

    llama_context_params cp = llama_context_default_params();
    cp.embeddings = true;
    cp.n_ctx = 2048;
    cp.n_batch = 2048;
    cp.pooling_type = LLAMA_POOLING_TYPE_UNSPECIFIED; // honor GGUF metadata (BGE: CLS)
    m_ctx = llama_init_from_model(m_model, cp);
    if (!m_ctx) {
        qWarning() << "[Embedding] context init failed";
        llama_model_free(m_model);
        m_model = nullptr;
        return false;
    }

    m_dim = llama_model_n_embd(m_model);
    qDebug() << "[Embedding] model ready, dim =" << m_dim;
    return m_dim > 0;
}

bool EmbeddingEngine::isModelLoaded() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_model != nullptr && m_ctx != nullptr;
}

int EmbeddingEngine::dimension() const
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    return m_dim;
}

QVector<float> EmbeddingEngine::embedText(const QString &text)
{
    QMutexLocker<QRecursiveMutex> locker(&m_mutex);
    if (!m_model || !m_ctx)
        return {};

    const std::string utf8 = text.trimmed().toStdString();
    if (utf8.empty())
        return {};

    const llama_vocab *vocab = llama_model_get_vocab(m_model);
    if (!vocab)
        return {};

    const int needed = -llama_tokenize(vocab, utf8.c_str(), static_cast<int32_t>(utf8.size()),
                                       nullptr, 0, true, true);
    if (needed <= 0)
        return {};
    std::vector<llama_token> tokens(static_cast<size_t>(needed));
    if (llama_tokenize(vocab, utf8.c_str(), static_cast<int32_t>(utf8.size()),
                       tokens.data(), needed, true, true) < 0)
        return {};

    const int maxTokens = static_cast<int>(llama_n_ctx(m_ctx)) - 8;
    if (static_cast<int>(tokens.size()) > maxTokens)
        tokens.resize(static_cast<size_t>(maxTokens));

    llama_memory_t mem = llama_get_memory(m_ctx);
    llama_memory_seq_rm(mem, -1, -1, -1);

    llama_batch batch = llama_batch_init(static_cast<int32_t>(tokens.size()), 0, 1);
    for (size_t i = 0; i < tokens.size(); ++i) {
        batch.token[batch.n_tokens] = tokens[i];
        batch.pos[batch.n_tokens] = static_cast<llama_pos>(i);
        batch.n_seq_id[batch.n_tokens] = 1;
        batch.seq_id[batch.n_tokens][0] = 0;
        batch.logits[batch.n_tokens] = true;
        batch.n_tokens++;
    }

    QVector<float> out;
    if (llama_decode(m_ctx, batch) == 0) {
        const float *emb = nullptr;
        if (llama_pooling_type(m_ctx) != LLAMA_POOLING_TYPE_NONE)
            emb = llama_get_embeddings_seq(m_ctx, 0);
        if (!emb)
            emb = llama_get_embeddings_ith(m_ctx, batch.n_tokens - 1);
        if (emb && m_dim > 0) {
            out.resize(m_dim);
            double norm = 0.0;
            for (int i = 0; i < m_dim; ++i)
                norm += static_cast<double>(emb[i]) * emb[i];
            norm = std::sqrt(std::max(norm, 1e-12));
            for (int i = 0; i < m_dim; ++i)
                out[i] = static_cast<float>(emb[i] / norm);
        }
    }
    llama_batch_free(batch);
    return out;
}

float EmbeddingEngine::similarity(const QVector<float> &a, const QVector<float> &b)
{
    if (a.isEmpty() || a.size() != b.size())
        return -1.0f;
    double dot = 0.0;
    for (int i = 0; i < a.size(); ++i)
        dot += static_cast<double>(a[i]) * b[i];
    return static_cast<float>(dot);
}

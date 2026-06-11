#ifndef EMBEDDINGENGINE_H
#define EMBEDDINGENGINE_H

#include <QRecursiveMutex>
#include <QString>
#include <QVector>
#include <string>

struct llama_model;
struct llama_context;

/// Dedicated embedding model (e.g. BGE-M3 GGUF) for true semantic search.
/// Produces L2-normalized vectors, so similarity is a plain dot product.
/// All calls are serialized internally; safe from worker threads.
class EmbeddingEngine {
public:
    EmbeddingEngine() = default;
    ~EmbeddingEngine();

    bool loadModel(const std::string &modelPath);
    bool isModelLoaded() const;
    int dimension() const;

    /// Embed UTF-8 text (truncated to the model context). Empty vector on failure.
    QVector<float> embedText(const QString &text);

    /// Cosine similarity for L2-normalized vectors (dot product).
    static float similarity(const QVector<float> &a, const QVector<float> &b);

private:
    void unload();

    mutable QRecursiveMutex m_mutex;
    llama_model *m_model = nullptr;
    llama_context *m_ctx = nullptr;
    int m_dim = 0;
};

#endif // EMBEDDINGENGINE_H

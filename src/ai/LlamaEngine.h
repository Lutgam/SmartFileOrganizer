#ifndef LLAMAENGINE_H
#define LLAMAENGINE_H

#include "llama.h"
#include <QObject>
#include <QTimer>
#include <QRecursiveMutex>
#include <QString>
#include <atomic>
#include <string>
#include <vector>

class LlamaEngine : public QObject
{
    Q_OBJECT
public:
    explicit LlamaEngine(QObject *parent = nullptr);
    ~LlamaEngine() override;

    bool loadModel(const std::string& modelPath);
    bool isModelLoaded() const;

    static constexpr int kMaxNewTokensDefault = 128;
    static constexpr int kMaxNewTokensSemanticRetriever = 150;
    static constexpr int kMaxNewTokensTagClusterJson = 400;
    static constexpr int kMaxNewTokensSuggestTagsJson = 600;

    std::string generateResponse(const std::string &prompt, int maxNewTokens = kMaxNewTokensDefault);
    std::string suggestTags(const std::string& filename,
                            const std::string& content,
                            const std::string& rejectedTagsCsv = "",
                            const std::string& existingTags = "",
                            bool contentReadable = true,
                            const std::string& fileExt = "");
    void setCancelFlag(std::atomic<bool>* flag) { m_cancelFlag = flag; } // Link to UI cancel flag
    void setOutputLanguage(const QString &lang);

private:
    friend struct InferenceGuard;
    bool loadModelImpl(const std::string &modelPath);
    void unloadModel();
    bool ensureModelLoaded();
    void stopIdleTimerAsync();
    void startIdleTimerAsync();

    /// Serialized with `m_mutex` / `InferenceGuard`; safe to call from worker threads (never use BlockingQueuedConnection).
    std::string generateResponseImpl(const std::string &prompt, int maxNewTokens);
    std::string suggestTagsImpl(const std::string &filename,
                                const std::string &content,
                                const std::string &rejectedTagsCsv,
                                const std::string &existingTags,
                                bool contentReadable,
                                const std::string &fileExt);

    QTimer* idleTimer = nullptr;
    std::string m_modelPath;
    int m_activeInferences = 0;
    mutable QRecursiveMutex m_mutex;

    struct llama_model* model = nullptr;
    struct llama_context* ctx = nullptr;
    std::atomic<bool>* m_cancelFlag = nullptr; // Points to MainWindow's flag (not owned)

    QString m_currentLanguage = QStringLiteral("zh_TW");
};

#endif // LLAMAENGINE_H

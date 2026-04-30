#ifndef LLAMAENGINE_H
#define LLAMAENGINE_H

#include "llama.h"
#include <QObject>
#include <QTimer>
#include <QMutex>
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
    bool isModelLoaded() const { return model != nullptr; }
    std::string generateResponse(const std::string& prompt);
    std::string suggestTags(const std::string& filename,
                            const std::string& content,
                            const std::string& rejectedTagsCsv = "",
                            const std::string& existingTags = "");
    void setCancelFlag(std::atomic<bool>* flag) { m_cancelFlag = flag; } // Link to UI cancel flag

private:
    friend struct InferenceGuard;
    void unloadModel();
    bool ensureModelLoaded();
    void stopIdleTimerAsync();
    void startIdleTimerAsync();

    QTimer* idleTimer = nullptr;
    std::string m_modelPath;
    int m_activeInferences = 0;
    mutable QMutex m_mutex;

    struct llama_model* model = nullptr;
    struct llama_context* ctx = nullptr;
    std::atomic<bool>* m_cancelFlag = nullptr; // Points to MainWindow's flag (not owned)
};

#endif // LLAMAENGINE_H

#ifndef LLAMAENGINE_H
#define LLAMAENGINE_H

#include "llama.h"
#include <atomic>
#include <string>
#include <vector>

class LlamaEngine
{
public:
    LlamaEngine();
    ~LlamaEngine();

    bool loadModel(const std::string& modelPath);
    bool isModelLoaded() const { return model != nullptr; }
    std::string generateResponse(const std::string& prompt);
    std::string suggestTags(const std::string& filename, const std::string& content, const std::string& existingTags = "");
    void setCancelFlag(std::atomic<bool>* flag) { m_cancelFlag = flag; } // Link to UI cancel flag

private:
    struct llama_model* model = nullptr;
    struct llama_context* ctx = nullptr;
    std::atomic<bool>* m_cancelFlag = nullptr; // Points to MainWindow's flag (not owned)
};

#endif // LLAMAENGINE_H

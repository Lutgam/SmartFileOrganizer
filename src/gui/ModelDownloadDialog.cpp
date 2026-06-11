#include "ModelDownloadDialog.h"
#include "LanguageManager.h"

#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
// Recommended defaults: small enough for consumer hardware, strong zh-TW support.
constexpr const char kLlmUrl[] =
    "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/main/qwen2.5-3b-instruct-q4_k_m.gguf";
constexpr const char kEmbeddingUrl[] =
    "https://huggingface.co/CompendiumLabs/bge-m3-gguf/resolve/main/bge-m3-q4_k_m.gguf";
constexpr qint64 kLlmMinBytes = 1500LL * 1024 * 1024;       // ~1.9 GB actual
constexpr qint64 kEmbeddingMinBytes = 300LL * 1024 * 1024;  // ~440 MB actual
} // namespace

ModelDownloadDialog::ModelDownloadDialog(const QString &destDir, QWidget *parent)
    : QDialog(parent), m_destDir(QDir::cleanPath(destDir))
{
    auto &lm = LanguageManager::instance();
    setWindowTitle(lm.getText(QStringLiteral("model_dl_title")));
    setModal(true);
    resize(560, 360);

    auto *root = new QVBoxLayout(this);

    auto *intro = new QLabel(lm.getText(QStringLiteral("model_dl_intro")), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    m_chkLlm = new QCheckBox(
        QStringLiteral("Qwen2.5-3B-Instruct (Q4_K_M, ~1.9 GB) — %1")
            .arg(lm.getText(QStringLiteral("model_dl_llm_role"))),
        this);
    m_chkLlm->setChecked(true);
    root->addWidget(m_chkLlm);

    m_chkEmbedding = new QCheckBox(
        QStringLiteral("BGE-M3 (Q4_K_M, ~440 MB) — %1")
            .arg(lm.getText(QStringLiteral("model_dl_embedding_role"))),
        this);
    m_chkEmbedding->setChecked(true);
    root->addWidget(m_chkEmbedding);

    auto *privacy = new QLabel(lm.getText(QStringLiteral("model_dl_privacy_note")), this);
    privacy->setWordWrap(true);
    privacy->setStyleSheet(QStringLiteral("color: rgba(120,180,120,255); font-size: 12px;"));
    root->addWidget(privacy);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    root->addWidget(m_progress);

    m_statusLabel = new QLabel(QString(), this);
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    m_logLabel = new QLabel(QString(), this);
    m_logLabel->setWordWrap(true);
    m_logLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: rgba(255,255,255,140);"));
    root->addWidget(m_logLabel, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    m_btnStart = new QPushButton(lm.getText(QStringLiteral("model_dl_start")), this);
    connect(m_btnStart, &QPushButton::clicked, this, [this]() {
        m_queue.clear();
        if (m_chkLlm->isChecked()) {
            m_queue.append({QStringLiteral("Qwen2.5-3B-Instruct"), QUrl(QString::fromLatin1(kLlmUrl)),
                            QStringLiteral("chat_model.gguf"), kLlmMinBytes, false});
        }
        if (m_chkEmbedding->isChecked()) {
            m_queue.append({QStringLiteral("BGE-M3"), QUrl(QString::fromLatin1(kEmbeddingUrl)),
                            QStringLiteral("embedding_model.gguf"), kEmbeddingMinBytes, true});
        }
        if (m_queue.isEmpty())
            return;
        m_btnStart->setEnabled(false);
        m_chkLlm->setEnabled(false);
        m_chkEmbedding->setEnabled(false);
        m_currentIndex = -1;
        startNextDownload();
    });
    btnRow->addWidget(m_btnStart);

    m_btnCancel = new QPushButton(lm.getText(QStringLiteral("model_dl_cancel")), this);
    connect(m_btnCancel, &QPushButton::clicked, this, [this]() {
        if (m_reply)
            m_reply->abort();
        reject();
    });
    btnRow->addWidget(m_btnCancel);
    root->addLayout(btnRow);

    m_nam = new QNetworkAccessManager(this);
}

void ModelDownloadDialog::appendLog(const QString &line)
{
    if (!m_logLabel)
        return;
    QString t = m_logLabel->text();
    if (!t.isEmpty())
        t += QStringLiteral("\n");
    m_logLabel->setText(t + line);
}

bool ModelDownloadDialog::looksLikeValidGguf(const QString &path, qint64 minBytes)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    if (f.size() < minBytes)
        return false;
    const QByteArray magic = f.read(4);
    return magic == QByteArrayLiteral("GGUF");
}

void ModelDownloadDialog::startNextDownload()
{
    ++m_currentIndex;
    if (m_currentIndex >= m_queue.size()) {
        m_statusLabel->setText(LanguageManager::instance().getText(QStringLiteral("model_dl_all_done")));
        m_btnCancel->setText(QStringLiteral("OK"));
        accept();
        return;
    }

    const DownloadItem &item = m_queue[m_currentIndex];
    if (!QDir().mkpath(m_destDir)) {
        m_statusLabel->setText(QStringLiteral("❌ mkpath failed: %1").arg(m_destDir));
        return;
    }
    const QString destPath = QDir(m_destDir).absoluteFilePath(item.destFileName);
    const QString tmpPath = destPath + QStringLiteral(".part");

    m_outFile = new QFile(tmpPath, this);
    if (!m_outFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_statusLabel->setText(QStringLiteral("❌ cannot write: %1").arg(tmpPath));
        return;
    }

    m_statusLabel->setText(
        LanguageManager::instance().getText(QStringLiteral("model_dl_downloading")).arg(item.label));
    m_progress->setValue(0);

    QNetworkRequest req(item.url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam->get(req);

    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_outFile && m_reply)
            m_outFile->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0)
            m_progress->setValue(static_cast<int>(got * 100 / total));
    });
    connect(m_reply, &QNetworkReply::finished, this, &ModelDownloadDialog::onDownloadFinished);
}

void ModelDownloadDialog::onDownloadFinished()
{
    auto &lm = LanguageManager::instance();
    const DownloadItem item = m_queue[m_currentIndex];
    const QString destPath = QDir(m_destDir).absoluteFilePath(item.destFileName);
    const QString tmpPath = destPath + QStringLiteral(".part");

    if (m_outFile) {
        if (m_reply)
            m_outFile->write(m_reply->readAll());
        m_outFile->close();
        m_outFile->deleteLater();
        m_outFile = nullptr;
    }

    const bool netOk = m_reply && m_reply->error() == QNetworkReply::NoError;
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    if (!netOk) {
        QFile::remove(tmpPath);
        m_statusLabel->setText(lm.getText(QStringLiteral("model_dl_failed")).arg(item.label));
        m_btnStart->setEnabled(true);
        m_chkLlm->setEnabled(true);
        m_chkEmbedding->setEnabled(true);
        return;
    }

    if (!looksLikeValidGguf(tmpPath, item.minExpectedBytes)) {
        QFile::remove(tmpPath);
        m_statusLabel->setText(lm.getText(QStringLiteral("model_dl_verify_failed")).arg(item.label));
        m_btnStart->setEnabled(true);
        m_chkLlm->setEnabled(true);
        m_chkEmbedding->setEnabled(true);
        return;
    }

    // Verified: atomically move into place and record checksum for the log.
    QFile::remove(destPath);
    QFile::rename(tmpPath, destPath);

    QString shaHex;
    {
        QFile f(destPath);
        if (f.open(QIODevice::ReadOnly)) {
            QCryptographicHash h(QCryptographicHash::Sha256);
            if (h.addData(&f))
                shaHex = QString::fromLatin1(h.result().toHex().left(16));
        }
    }
    appendLog(QStringLiteral("✅ %1 → %2 (GGUF OK, sha256:%3…)")
                  .arg(item.label, destPath, shaHex));

    QSettings s;
    if (item.isEmbedding) {
        m_downloadedEmbeddingPath = destPath;
        s.setValue(QStringLiteral("ai/embedding_model_path"), destPath);
    } else {
        m_downloadedLlmPath = destPath;
        s.setValue(QStringLiteral("ai/model_path"), destPath);
    }

    startNextDownload();
}

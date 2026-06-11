#ifndef MODELDOWNLOADDIALOG_H
#define MODELDOWNLOADDIALOG_H

#include <QDialog>
#include <QUrl>
#include <QVector>

class QCheckBox;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QPushButton;
class QFile;

/// First-run model installer: downloads the recommended LLM (and optional
/// embedding model) with progress + GGUF integrity verification. This is the
/// ONLY place in the app that touches the network; inference stays 100% local.
class ModelDownloadDialog : public QDialog {
    Q_OBJECT

public:
    explicit ModelDownloadDialog(const QString &destDir, QWidget *parent = nullptr);

    /// Absolute path of the downloaded chat model (empty if not downloaded).
    QString downloadedLlmPath() const { return m_downloadedLlmPath; }
    QString downloadedEmbeddingPath() const { return m_downloadedEmbeddingPath; }

private:
    struct DownloadItem {
        QString label;
        QUrl url;
        QString destFileName;
        qint64 minExpectedBytes = 0;
        bool isEmbedding = false;
    };

    void startNextDownload();
    void onDownloadFinished();
    void appendLog(const QString &line);
    static bool looksLikeValidGguf(const QString &path, qint64 minBytes);

    QString m_destDir;
    QVector<DownloadItem> m_queue;
    int m_currentIndex = -1;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_outFile = nullptr;

    QCheckBox *m_chkLlm = nullptr;
    QCheckBox *m_chkEmbedding = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_logLabel = nullptr;
    QPushButton *m_btnStart = nullptr;
    QPushButton *m_btnCancel = nullptr;

    QString m_downloadedLlmPath;
    QString m_downloadedEmbeddingPath;
};

#endif // MODELDOWNLOADDIALOG_H

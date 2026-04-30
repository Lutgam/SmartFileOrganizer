#ifndef DUPLICATECLEANERDIALOG_H
#define DUPLICATECLEANERDIALOG_H

#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>
#include <atomic>

class QLabel;
class QProgressBar;
class QTreeWidget;
class QPushButton;
class QLineEdit;
template <typename T> class QFutureWatcher;

class DuplicateCleanerWidget : public QWidget {
    Q_OBJECT

public:
    struct DuplicateGroup {
        QString hashHex;
        qint64 size = 0;
        QStringList files;
    };

    explicit DuplicateCleanerWidget(QWidget *parent = nullptr);
    ~DuplicateCleanerWidget() override;

    void setSuggestedPath(const QString &targetPath);

signals:
    // [oldPath, newPath] moved into staging area
    void cleanupCompleted(const QList<QPair<QString, QString>> &movedHistory);

private:
    void startScan();
    void populateTree(const QList<DuplicateGroup> &groups);
    void applyDefaultChecks();
    void moveCheckedToStaging();
    void requestStop();
    void onBrowseClicked();
    void onStartClicked();

    QString m_targetPath;
    QLineEdit *m_pathLineEdit = nullptr;
    QPushButton *m_btnBrowse = nullptr;
    QPushButton *m_btnStartScan = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QTreeWidget *tree = nullptr;
    QPushButton *btnMoveToStaging = nullptr;
    QPushButton *btnStopScan = nullptr;
    QFutureWatcher<QList<DuplicateGroup>> *watcher = nullptr;

    QList<QPair<QString, QString>> m_movedHistory;

    std::atomic<bool> m_cancelRequested{false};
};

#endif // DUPLICATECLEANERDIALOG_H

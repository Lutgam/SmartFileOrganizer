#ifndef DUPLICATECLEANERDIALOG_H
#define DUPLICATECLEANERDIALOG_H

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>
#include <atomic>

class QTreeWidget;
class QPushButton;
template <typename T> class QFutureWatcher;
class QProgressDialog;

class DuplicateCleanerDialog : public QDialog {
    Q_OBJECT

public:
    struct DuplicateGroup {
        QString hashHex;
        qint64 size = 0;
        QStringList files;
    };

    explicit DuplicateCleanerDialog(const QString &targetPath, QWidget *parent = nullptr);
    ~DuplicateCleanerDialog() override;

    // [oldPath, newPath] moved into staging area
    QList<QPair<QString, QString>> movedHistory() const;

private:
    void startScan();
    void populateTree(const QList<DuplicateGroup> &groups);
    void applyDefaultChecks();
    void moveCheckedToStaging();

    QString m_targetPath;
    QTreeWidget *tree = nullptr;
    QPushButton *btnMoveToStaging = nullptr;
    QPushButton *btnCancel = nullptr;

    QProgressDialog *progress = nullptr;
    QFutureWatcher<QList<DuplicateGroup>> *watcher = nullptr;

    QList<QPair<QString, QString>> m_movedHistory;

    std::atomic<bool> m_cancelRequested{false};
};

#endif // DUPLICATECLEANERDIALOG_H

#ifndef DUPLICATECLEANERDIALOG_H
#define DUPLICATECLEANERDIALOG_H

#include <QDialog>
#include <QString>

class QTreeWidget;
class QPushButton;

class DuplicateCleanerDialog : public QDialog {
    Q_OBJECT

public:
    explicit DuplicateCleanerDialog(const QString &targetPath, QWidget *parent = nullptr);
    ~DuplicateCleanerDialog() override;

private:
    void scanAndPopulate();

    QString m_targetPath;
    QTreeWidget *tree = nullptr;
    QPushButton *btnClose = nullptr;
};

#endif // DUPLICATECLEANERDIALOG_H

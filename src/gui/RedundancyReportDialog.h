#ifndef REDUNDANCYREPORTDIALOG_H
#define REDUNDANCYREPORTDIALOG_H

#include <QDialog>
#include <QMap>
#include <QSet>
#include <QStringList>

class QLabel;
class QTreeWidget;

/// Grouped redundancy: same-content (hash) vs same-name / different hash. Check files to delete.
class RedundancyReportDialog : public QDialog {
    Q_OBJECT
public:
    explicit RedundancyReportDialog(QWidget *parent,
                                    int filesAnalyzed,
                                    int newTagAdds,
                                    int hashDuplicatePathCount,
                                    int nameConflictPathCount,
                                    const QMap<QString, QSet<QString>> &hashToPaths,
                                    const QMap<QString, QSet<QString>> &baseNameToPaths);

signals:
    void redundantFilesRemoved(const QStringList &absolutePaths);

private slots:
    void onExecuteDelete();

private:
    void appendHashSection(const QMap<QString, QSet<QString>> &hashToPaths);
    void appendNameConflictSection(const QMap<QString, QSet<QString>> &baseNameToPaths);

    QTreeWidget *m_tree = nullptr;
};

#endif // REDUNDANCYREPORTDIALOG_H

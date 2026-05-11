#ifndef REDUNDANCYREPORTDIALOG_H
#define REDUNDANCYREPORTDIALOG_H

#include <QDialog>
#include <QStringList>

class QListWidget;
class QLabel;
class QPushButton;

/// Post–folder-analysis UI: redundant paths with checkboxes and optional bulk delete.
class RedundancyReportDialog : public QDialog {
    Q_OBJECT
public:
    /// \a redundantPaths should be absolute file paths (one row each).
    explicit RedundancyReportDialog(QWidget *parent,
                                    int filesAnalyzed,
                                    int newTagAdds,
                                    int redundantCount,
                                    const QStringList &redundantPaths);

signals:
    /// Emitted with paths successfully removed from disk (metadata should be updated by receiver).
    void redundantFilesRemoved(const QStringList &absolutePaths);

private slots:
    void onDeleteChecked();

private:
    QListWidget *m_list = nullptr;
};

#endif // REDUNDANCYREPORTDIALOG_H

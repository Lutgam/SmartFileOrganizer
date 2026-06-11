#ifndef RULESDIALOG_H
#define RULESDIALOG_H

#include <QDialog>
#include <QVector>

#include "../core/AutoRuleEngine.h"

class QTableWidget;
class QPushButton;

/// Manage deterministic auto-organize rules (stored in .smartfile/rules.json).
/// "套用" emits applyRequested with current matches; MainWindow previews and
/// performs the actual (journaled) mutations.
class RulesDialog : public QDialog {
    Q_OBJECT

public:
    explicit RulesDialog(const QString &workspaceRoot, QWidget *parent = nullptr);

    QVector<SfAutoRule> rules() const { return m_rules; }

signals:
    /// User asked to apply the current rule set across the workspace.
    void applyRequested(const QVector<SfAutoRule> &rules);

private:
    void reloadTable();
    void addRuleInteractive();
    void removeSelectedRule();
    void persist();
    QString rulesFilePath() const;

    QString m_workspaceRoot;
    QVector<SfAutoRule> m_rules;
    QTableWidget *m_table = nullptr;
};

#endif // RULESDIALOG_H

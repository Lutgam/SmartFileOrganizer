#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QComboBox;
class QCheckBox;
class QLineEdit;
class QPushButton;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const QString &currentRootPath, QWidget *parent = nullptr);

    bool settingsChanged() const;
    int selectedLanguageIndex() const; // 0: ZH_TW, 1: EN_US
    QString modelPath() const;
    bool backgroundAutoAnalysis() const;

signals:
    void settingsApplied();

private:
    void loadFromSettings();
    void saveToSettings();
    void browseModel();

    QString m_rootPath;
    bool m_changed = false;

    QComboBox *m_languageCombo = nullptr;
    QLineEdit *m_modelPathEdit = nullptr;
    QPushButton *m_browseBtn = nullptr;
    QCheckBox *m_bgAutoAnalyze = nullptr;
};

#endif


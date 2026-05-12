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
    /// When true, skip LLM for shortcuts / executables / archives (workspace/system_file_bypass_filter).
    bool systemFileBypassFilter() const;
    /// 0 = disabled, otherwise years (1, 3, or 5) for cold-archive skip.
    int coldArchiveYears() const;

signals:
    void settingsApplied();
    void clearAiCacheRequested();
    void clearHashCacheRequested();
    void factoryResetRequested();

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
    QCheckBox *m_systemFileBypass = nullptr;
    QComboBox *m_coldArchiveCombo = nullptr;

    QPushButton *m_btnClearAi = nullptr;
    QPushButton *m_btnClearHash = nullptr;
    QPushButton *m_btnFactoryReset = nullptr;
};

#endif

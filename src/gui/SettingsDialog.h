#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QWidget>

class QComboBox;
class QCheckBox;
class QLineEdit;
class QPushButton;

class SettingsPanel : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPanel(const QString &currentRootPath, QWidget *parent = nullptr);

    void setRootPath(const QString &path);
    void applyAndSave();

    bool settingsChanged() const;
    int selectedLanguageIndex() const;
    QString modelPath() const;
    bool backgroundAutoAnalysis() const;
    bool systemFileBypassFilter() const;
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
    void refreshDataActionButtons();

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
    QPushButton *m_btnSave = nullptr;
};

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const QString &currentRootPath, QWidget *parent = nullptr);

    bool settingsChanged() const;
    int selectedLanguageIndex() const;
    QString modelPath() const;
    bool backgroundAutoAnalysis() const;
    bool systemFileBypassFilter() const;
    int coldArchiveYears() const;

signals:
    void settingsApplied();
    void clearAiCacheRequested();
    void clearHashCacheRequested();
    void factoryResetRequested();

private:
    SettingsPanel *m_panel = nullptr;
};

#endif

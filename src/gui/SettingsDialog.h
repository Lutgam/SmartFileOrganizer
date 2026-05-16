#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QTime>
#include <QWidget>

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimeEdit;
class QGroupBox;

class SettingsPanel : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPanel(const QString &currentRootPath, QWidget *parent = nullptr);

    void setRootPath(const QString &path);
    void applyAndSave();
    void applyLocalizedTexts();

    bool settingsChanged() const;
    int selectedLanguageIndex() const;
    QString modelPath() const;
    bool backgroundAutoAnalysis() const;
    bool systemFileBypassFilter() const;
    int coldArchiveYears() const;
    int concurrencyLimit() const;
    bool o1CacheBypassEnabled() const;
    bool timeScheduleEnabled() const;
    QTime scheduleStartTime() const;
    QTime scheduleEndTime() const;

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
    void syncScheduleTimeEditEnabled();
    void applyConcurrencyToThreadPool(int limit);

    QString m_rootPath;
    bool m_changed = false;

    QComboBox *m_languageCombo = nullptr;
    QLineEdit *m_modelPathEdit = nullptr;
    QPushButton *m_browseBtn = nullptr;
    QCheckBox *m_bgAutoAnalyze = nullptr;
    QCheckBox *m_systemFileBypass = nullptr;
    QComboBox *m_coldArchiveCombo = nullptr;

    QSpinBox *m_concurrencySpin = nullptr;
    QSpinBox *m_llamaCpuThreadsSpin = nullptr;
    QCheckBox *m_o1CacheBypass = nullptr;
    QCheckBox *m_enableTimeSchedule = nullptr;
    QTimeEdit *m_scheduleStart = nullptr;
    QTimeEdit *m_scheduleEnd = nullptr;

    QPushButton *m_btnClearAi = nullptr;
    QPushButton *m_btnClearHash = nullptr;
    QPushButton *m_btnFactoryReset = nullptr;
    QPushButton *m_btnSave = nullptr;

    QLabel *m_lblLanguage = nullptr;
    QLabel *m_lblModel = nullptr;
    QLabel *m_lblColdArchive = nullptr;
    QGroupBox *m_perfSchedulingGroup = nullptr;
    QLabel *m_lblAiConcurrency = nullptr;
    QLabel *m_lblLlamaCpuThreads = nullptr;
    QLabel *m_lblScheduleStart = nullptr;
    QLabel *m_lblScheduleEnd = nullptr;
    QGroupBox *m_dataMgmtGroup = nullptr;
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

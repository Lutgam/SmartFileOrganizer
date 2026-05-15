#include "SettingsDialog.h"

#include "LanguageManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QThread>
#include <QThreadPool>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace {
static constexpr const char *kSettingsModelPathKey = "ai/model_path";
static constexpr const char *kSettingsBgAutoAnalyze = "workspace/background_auto_analysis";
static constexpr const char *kSettingsSystemFileBypass = "workspace/system_file_bypass_filter";
static constexpr const char *kSettingsColdArchiveYears = "workspace/cold_archive_years";
static constexpr const char *kSettingsAiConcurrency = "ai/concurrency_limit";
static constexpr const char *kSettingsO1CacheBypass = "analysis/enable_o1_cache_bypass";
static constexpr const char *kSettingsTimeSchedule = "analysis/enable_time_schedule";
static constexpr const char *kSettingsScheduleStart = "analysis/schedule_start_time";
static constexpr const char *kSettingsScheduleEnd = "analysis/schedule_end_time";

QString defaultBundledModelPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString candidates[] = {
        QDir(appDir).filePath(QStringLiteral("assets/models/chat_model.gguf")),
        QDir(appDir).filePath(QStringLiteral("../assets/models/chat_model.gguf")),
    };
    for (const QString &p : candidates) {
        const QString clean = QDir::cleanPath(p);
        if (QFile::exists(clean))
            return clean;
    }
    return QDir::cleanPath(QDir(appDir).filePath(QStringLiteral("assets/models/chat_model.gguf")));
}
} // namespace

SettingsPanel::SettingsPanel(const QString &currentRootPath, QWidget *parent)
    : QWidget(parent), m_rootPath(currentRootPath) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    {
        auto *row = new QHBoxLayout();
        m_lblLanguage = new QLabel(this);
        row->addWidget(m_lblLanguage);
        m_languageCombo = new QComboBox(this);
        row->addWidget(m_languageCombo, 1);
        root->addLayout(row);
    }

    {
        auto *row = new QHBoxLayout();
        m_lblModel = new QLabel(this);
        row->addWidget(m_lblModel);
        m_modelPathEdit = new QLineEdit(this);
        row->addWidget(m_modelPathEdit, 1);
        m_browseBtn = new QPushButton(this);
        row->addWidget(m_browseBtn);
        root->addLayout(row);
    }

    {
        m_bgAutoAnalyze = new QCheckBox(this);
        root->addWidget(m_bgAutoAnalyze);
    }

    {
        m_systemFileBypass = new QCheckBox(
            LanguageManager::instance().getText(QStringLiteral("settings_system_file_bypass")), this);
        m_systemFileBypass->setChecked(true);
        root->addWidget(m_systemFileBypass);
    }

    {
        auto *row = new QHBoxLayout();
        m_lblColdArchive = new QLabel(this);
        row->addWidget(m_lblColdArchive);
        m_coldArchiveCombo = new QComboBox(this);
        row->addWidget(m_coldArchiveCombo, 1);
        root->addLayout(row);
    }

    {
        m_perfSchedulingGroup = new QGroupBox(this);
        auto *gv = new QVBoxLayout(m_perfSchedulingGroup);

        {
            auto *row = new QHBoxLayout();
            m_lblAiConcurrency = new QLabel(m_perfSchedulingGroup);
            row->addWidget(m_lblAiConcurrency);
            m_concurrencySpin = new QSpinBox(m_perfSchedulingGroup);
            const int ideal = qMax(1, QThread::idealThreadCount());
            m_concurrencySpin->setRange(1, ideal);
            m_concurrencySpin->setValue(2);
            row->addWidget(m_concurrencySpin);
            row->addStretch(1);
            gv->addLayout(row);
        }

        m_o1CacheBypass = new QCheckBox(m_perfSchedulingGroup);
        m_o1CacheBypass->setChecked(true);
        gv->addWidget(m_o1CacheBypass);

        m_enableTimeSchedule = new QCheckBox(m_perfSchedulingGroup);
        gv->addWidget(m_enableTimeSchedule);

        {
            auto *row = new QHBoxLayout();
            m_lblScheduleStart = new QLabel(m_perfSchedulingGroup);
            row->addWidget(m_lblScheduleStart);
            m_scheduleStart = new QTimeEdit(m_perfSchedulingGroup);
            m_scheduleStart->setDisplayFormat(QStringLiteral("HH:mm"));
            m_scheduleStart->setTime(QTime(2, 0));
            row->addWidget(m_scheduleStart);
            m_lblScheduleEnd = new QLabel(m_perfSchedulingGroup);
            row->addWidget(m_lblScheduleEnd);
            m_scheduleEnd = new QTimeEdit(m_perfSchedulingGroup);
            m_scheduleEnd->setDisplayFormat(QStringLiteral("HH:mm"));
            m_scheduleEnd->setTime(QTime(6, 0));
            row->addWidget(m_scheduleEnd);
            row->addStretch(1);
            gv->addLayout(row);
        }

        root->addWidget(m_perfSchedulingGroup);

        connect(m_concurrencySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            applyConcurrencyToThreadPool(value);
            QSettings s;
            s.setValue(QString::fromLatin1(kSettingsAiConcurrency), value);
        });
        connect(m_enableTimeSchedule, &QCheckBox::toggled, this, &SettingsPanel::syncScheduleTimeEditEnabled);
    }

    {
        m_dataMgmtGroup = new QGroupBox(this);
        auto *gv = new QVBoxLayout(m_dataMgmtGroup);
        m_btnClearAi = new QPushButton(m_dataMgmtGroup);
        m_btnClearHash = new QPushButton(m_dataMgmtGroup);
        m_btnFactoryReset = new QPushButton(m_dataMgmtGroup);
        gv->addWidget(m_btnClearAi);
        gv->addWidget(m_btnClearHash);
        gv->addWidget(m_btnFactoryReset);
        root->addWidget(m_dataMgmtGroup);

        connect(m_btnClearAi, &QPushButton::clicked, this, [this]() {
            const int r = QMessageBox::warning(this,
                                                 tr("確認清除"),
                                                 tr("將移除帶有 [AI] 前綴的標籤與 AI 摘要／雜湊分析快取，並保留其他系統／手動標籤；各檔案路徑與已儲存的 content SHA-256 仍會保留。\n\n確定要執行嗎？"),
                                                 QMessageBox::Yes | QMessageBox::No,
                                                 QMessageBox::No);
            if (r != QMessageBox::Yes) return;
            emit clearAiCacheRequested();
        });
        connect(m_btnClearHash, &QPushButton::clicked, this, [this]() {
            const int r = QMessageBox::warning(this,
                                                 tr("確認清除"),
                                                 tr("將清除所有檔案的 SHA-256 紀錄與雜湊分析快取。下次分析會重新計算雜湊。\n\n確定要執行嗎？"),
                                                 QMessageBox::Yes | QMessageBox::No,
                                                 QMessageBox::No);
            if (r != QMessageBox::Yes) return;
            emit clearHashCacheRequested();
        });
        connect(m_btnFactoryReset, &QPushButton::clicked, this, [this]() {
            const int r = QMessageBox::warning(this,
                                                 tr("危險操作"),
                                                 tr("將徹底清空標籤、雜湊、快取與拒絕標籤清單，並刪除磁碟上的 metadata.json。\n此動作無法復原。\n\n確定要執行嗎？"),
                                                 QMessageBox::Yes | QMessageBox::No,
                                                 QMessageBox::No);
            if (r != QMessageBox::Yes) return;
            emit factoryResetRequested();
        });
    }

    m_btnSave = new QPushButton(this);
    root->addWidget(m_btnSave, 0, Qt::AlignRight);
    root->addStretch(1);

    loadFromSettings();
    syncScheduleTimeEditEnabled();
    refreshDataActionButtons();
    applyLocalizedTexts();

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this,
            [this](LanguageManager::Language) { applyLocalizedTexts(); });

    connect(m_browseBtn, &QPushButton::clicked, this, &SettingsPanel::browseModel);
    connect(m_btnSave, &QPushButton::clicked, this, &SettingsPanel::applyAndSave);
}

void SettingsPanel::applyLocalizedTexts()
{
    auto &lm = LanguageManager::instance();
    if (m_lblLanguage)
        m_lblLanguage->setText(lm.getText(QStringLiteral("settings_language_label")));
    if (m_languageCombo) {
        const int idx = m_languageCombo->currentIndex();
        m_languageCombo->blockSignals(true);
        m_languageCombo->clear();
        m_languageCombo->addItem(lm.getText(QStringLiteral("settings_lang_zh")));
        m_languageCombo->addItem(lm.getText(QStringLiteral("settings_lang_en")));
        m_languageCombo->setCurrentIndex(qBound(0, idx, m_languageCombo->count() - 1));
        m_languageCombo->blockSignals(false);
    }
    if (m_lblModel)
        m_lblModel->setText(lm.getText(QStringLiteral("settings_model_label")));
    if (m_modelPathEdit)
        m_modelPathEdit->setPlaceholderText(lm.getText(QStringLiteral("settings_model_placeholder")));
    if (m_browseBtn)
        m_browseBtn->setText(lm.getText(QStringLiteral("瀏覽...")));
    if (m_bgAutoAnalyze)
        m_bgAutoAnalyze->setText(lm.getText(QStringLiteral("settings_bg_auto_analyze")));
    if (m_systemFileBypass)
        m_systemFileBypass->setText(lm.getText(QStringLiteral("settings_system_file_bypass")));
    if (m_lblColdArchive)
        m_lblColdArchive->setText(lm.getText(QStringLiteral("settings_cold_archive_prefix")));
    if (m_coldArchiveCombo) {
        const int y = m_coldArchiveCombo->currentData().toInt();
        m_coldArchiveCombo->blockSignals(true);
        m_coldArchiveCombo->clear();
        m_coldArchiveCombo->addItem(lm.getText(QStringLiteral("settings_cold_off")), 0);
        m_coldArchiveCombo->addItem(lm.getText(QStringLiteral("settings_cold_1y")), 1);
        m_coldArchiveCombo->addItem(lm.getText(QStringLiteral("settings_cold_3y")), 3);
        m_coldArchiveCombo->addItem(lm.getText(QStringLiteral("settings_cold_5y")), 5);
        int idx = m_coldArchiveCombo->findData(y);
        if (idx < 0)
            idx = 0;
        m_coldArchiveCombo->setCurrentIndex(idx);
        m_coldArchiveCombo->blockSignals(false);
    }
    if (m_perfSchedulingGroup)
        m_perfSchedulingGroup->setTitle(lm.getText(QStringLiteral("settings_perf_group")));
    if (m_lblAiConcurrency)
        m_lblAiConcurrency->setText(lm.getText(QStringLiteral("settings_ai_concurrency")));
    if (m_o1CacheBypass) {
        m_o1CacheBypass->setText(lm.getText(QStringLiteral("settings_o1_cache")));
        m_o1CacheBypass->setToolTip(lm.getText(QStringLiteral("settings_o1_cache_tooltip")));
    }
    if (m_enableTimeSchedule)
        m_enableTimeSchedule->setText(lm.getText(QStringLiteral("settings_time_schedule")));
    if (m_lblScheduleStart)
        m_lblScheduleStart->setText(lm.getText(QStringLiteral("settings_schedule_start")));
    if (m_lblScheduleEnd)
        m_lblScheduleEnd->setText(lm.getText(QStringLiteral("settings_schedule_end")));
    if (m_dataMgmtGroup)
        m_dataMgmtGroup->setTitle(lm.getText(QStringLiteral("settings_data_group")));
    if (m_btnClearAi)
        m_btnClearAi->setText(lm.getText(QStringLiteral("settings_clear_ai")));
    if (m_btnClearHash)
        m_btnClearHash->setText(lm.getText(QStringLiteral("settings_clear_hash")));
    if (m_btnFactoryReset)
        m_btnFactoryReset->setText(lm.getText(QStringLiteral("settings_factory_reset")));
    if (m_btnSave)
        m_btnSave->setText(lm.getText(QStringLiteral("settings_save")));
}

void SettingsPanel::applyConcurrencyToThreadPool(int limit)
{
    const int ideal = qMax(1, QThread::idealThreadCount());
    QThreadPool::globalInstance()->setMaxThreadCount(qBound(1, limit, ideal));
}

void SettingsPanel::syncScheduleTimeEditEnabled()
{
    const bool on = m_enableTimeSchedule && m_enableTimeSchedule->isChecked();
    if (m_scheduleStart)
        m_scheduleStart->setEnabled(on);
    if (m_scheduleEnd)
        m_scheduleEnd->setEnabled(on);
}

void SettingsPanel::setRootPath(const QString &path)
{
    m_rootPath = path;
    refreshDataActionButtons();
}

void SettingsPanel::applyAndSave()
{
    saveToSettings();
    emit settingsApplied();
}

bool SettingsPanel::settingsChanged() const {
    return m_changed;
}

int SettingsPanel::selectedLanguageIndex() const {
    return m_languageCombo ? m_languageCombo->currentIndex() : 0;
}

QString SettingsPanel::modelPath() const {
    return m_modelPathEdit ? m_modelPathEdit->text().trimmed() : QString();
}

bool SettingsPanel::backgroundAutoAnalysis() const {
    return m_bgAutoAnalyze && m_bgAutoAnalyze->isChecked();
}

bool SettingsPanel::systemFileBypassFilter() const {
    return m_systemFileBypass && m_systemFileBypass->isChecked();
}

int SettingsPanel::coldArchiveYears() const
{
    if (!m_coldArchiveCombo) return 0;
    return m_coldArchiveCombo->currentData().toInt();
}

int SettingsPanel::concurrencyLimit() const
{
    return m_concurrencySpin ? m_concurrencySpin->value() : 2;
}

bool SettingsPanel::o1CacheBypassEnabled() const
{
    return !m_o1CacheBypass || m_o1CacheBypass->isChecked();
}

bool SettingsPanel::timeScheduleEnabled() const
{
    return m_enableTimeSchedule && m_enableTimeSchedule->isChecked();
}

QTime SettingsPanel::scheduleStartTime() const
{
    return m_scheduleStart ? m_scheduleStart->time() : QTime(2, 0);
}

QTime SettingsPanel::scheduleEndTime() const
{
    return m_scheduleEnd ? m_scheduleEnd->time() : QTime(6, 0);
}

void SettingsPanel::loadFromSettings() {
    QSettings s;
    const auto lang = LanguageManager::instance().language();
    if (m_languageCombo) {
        m_languageCombo->setCurrentIndex(lang == LanguageManager::Language::EN_US ? 1 : 0);
    }
    if (m_modelPathEdit) {
        QString modelPath = s.value(QString::fromLatin1(kSettingsModelPathKey)).toString().trimmed();
        if (modelPath.isEmpty())
            modelPath = defaultBundledModelPath();
        m_modelPathEdit->setText(modelPath);
    }
    if (m_bgAutoAnalyze) {
        m_bgAutoAnalyze->setChecked(s.value(QString::fromLatin1(kSettingsBgAutoAnalyze), false).toBool());
    }
    if (m_systemFileBypass) {
        m_systemFileBypass->setChecked(s.value(QString::fromLatin1(kSettingsSystemFileBypass), true).toBool());
    }
    if (m_coldArchiveCombo) {
        const int y = s.value(QString::fromLatin1(kSettingsColdArchiveYears), 0).toInt();
        int idx = m_coldArchiveCombo->findData(y);
        if (idx < 0) idx = 0;
        m_coldArchiveCombo->setCurrentIndex(idx);
    }
    if (m_concurrencySpin) {
        const int ideal = qMax(1, QThread::idealThreadCount());
        const int c = qBound(1, s.value(QString::fromLatin1(kSettingsAiConcurrency), 2).toInt(), ideal);
        m_concurrencySpin->blockSignals(true);
        m_concurrencySpin->setValue(c);
        m_concurrencySpin->blockSignals(false);
        applyConcurrencyToThreadPool(c);
    }
    if (m_o1CacheBypass) {
        m_o1CacheBypass->setChecked(s.value(QString::fromLatin1(kSettingsO1CacheBypass), true).toBool());
    }
    if (m_enableTimeSchedule) {
        m_enableTimeSchedule->setChecked(s.value(QString::fromLatin1(kSettingsTimeSchedule), false).toBool());
    }
    if (m_scheduleStart) {
        m_scheduleStart->setTime(s.value(QString::fromLatin1(kSettingsScheduleStart), QTime(2, 0)).toTime());
    }
    if (m_scheduleEnd) {
        m_scheduleEnd->setTime(s.value(QString::fromLatin1(kSettingsScheduleEnd), QTime(6, 0)).toTime());
    }
}

void SettingsPanel::saveToSettings() {
    QSettings s;
    const QString prevModel = s.value(QString::fromLatin1(kSettingsModelPathKey)).toString();
    const QString newModel = modelPath();
    if (prevModel != newModel) {
        s.setValue(QString::fromLatin1(kSettingsModelPathKey), newModel);
        m_changed = true;
    }

    const auto prevLang = LanguageManager::instance().language();
    const auto newLang = (selectedLanguageIndex() == 1) ? LanguageManager::Language::EN_US : LanguageManager::Language::ZH_TW;
    if (prevLang != newLang) {
        LanguageManager::instance().setLanguage(newLang);
        m_changed = true;
    }

    const bool prevBg = s.value(QString::fromLatin1(kSettingsBgAutoAnalyze), false).toBool();
    const bool newBg = backgroundAutoAnalysis();
    if (prevBg != newBg) {
        s.setValue(QString::fromLatin1(kSettingsBgAutoAnalyze), newBg);
        m_changed = true;
    }

    const bool prevBypass = s.value(QString::fromLatin1(kSettingsSystemFileBypass), true).toBool();
    const bool newBypass = systemFileBypassFilter();
    if (prevBypass != newBypass) {
        s.setValue(QString::fromLatin1(kSettingsSystemFileBypass), newBypass);
        m_changed = true;
    }

    const int prevCold = s.value(QString::fromLatin1(kSettingsColdArchiveYears), 0).toInt();
    const int newCold = coldArchiveYears();
    if (prevCold != newCold) {
        s.setValue(QString::fromLatin1(kSettingsColdArchiveYears), newCold);
        m_changed = true;
    }

    const int prevConcurrency = s.value(QString::fromLatin1(kSettingsAiConcurrency), 2).toInt();
    const int newConcurrency = concurrencyLimit();
    if (prevConcurrency != newConcurrency) {
        s.setValue(QString::fromLatin1(kSettingsAiConcurrency), newConcurrency);
        applyConcurrencyToThreadPool(newConcurrency);
        m_changed = true;
    }

    const bool prevO1 = s.value(QString::fromLatin1(kSettingsO1CacheBypass), true).toBool();
    const bool newO1 = o1CacheBypassEnabled();
    if (prevO1 != newO1) {
        s.setValue(QString::fromLatin1(kSettingsO1CacheBypass), newO1);
        m_changed = true;
    }

    const bool prevSchedule = s.value(QString::fromLatin1(kSettingsTimeSchedule), false).toBool();
    const bool newSchedule = timeScheduleEnabled();
    if (prevSchedule != newSchedule) {
        s.setValue(QString::fromLatin1(kSettingsTimeSchedule), newSchedule);
        m_changed = true;
    }

    const QTime prevStart = s.value(QString::fromLatin1(kSettingsScheduleStart), QTime(2, 0)).toTime();
    const QTime newStart = scheduleStartTime();
    if (prevStart != newStart) {
        s.setValue(QString::fromLatin1(kSettingsScheduleStart), newStart);
        m_changed = true;
    }

    const QTime prevEnd = s.value(QString::fromLatin1(kSettingsScheduleEnd), QTime(6, 0)).toTime();
    const QTime newEnd = scheduleEndTime();
    if (prevEnd != newEnd) {
        s.setValue(QString::fromLatin1(kSettingsScheduleEnd), newEnd);
        m_changed = true;
    }
}

void SettingsPanel::browseModel() {
    const QString startDir = !modelPath().isEmpty() ? QFileInfo(modelPath()).absolutePath()
                          : (!m_rootPath.isEmpty() ? m_rootPath : QDir::homePath());
    const QString file = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select GGUF model"),
        startDir,
        QStringLiteral("GGUF models (*.gguf);;All files (*)"));
    if (file.isEmpty()) return;
    if (m_modelPathEdit) m_modelPathEdit->setText(QDir::cleanPath(file));
}

void SettingsPanel::refreshDataActionButtons()
{
    const bool allowData = !m_rootPath.trimmed().isEmpty();
    if (m_btnClearAi) m_btnClearAi->setEnabled(allowData);
    if (m_btnClearHash) m_btnClearHash->setEnabled(allowData);
    if (m_btnFactoryReset) m_btnFactoryReset->setEnabled(allowData);
}

SettingsDialog::SettingsDialog(const QString &currentRootPath, QWidget *parent)
    : QDialog(parent) {
    resize(640, 420);

    auto *root = new QVBoxLayout(this);
    m_panel = new SettingsPanel(currentRootPath, this);
    root->addWidget(m_panel);
    setWindowTitle(LanguageManager::instance().getText(QStringLiteral("settings_dialog_title")));

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this,
            [this](LanguageManager::Language) {
                setWindowTitle(LanguageManager::instance().getText(QStringLiteral("settings_dialog_title")));
            });

    connect(m_panel, &SettingsPanel::settingsApplied, this, &SettingsDialog::settingsApplied);
    connect(m_panel, &SettingsPanel::clearAiCacheRequested, this, &SettingsDialog::clearAiCacheRequested);
    connect(m_panel, &SettingsPanel::clearHashCacheRequested, this, &SettingsDialog::clearHashCacheRequested);
    connect(m_panel, &SettingsPanel::factoryResetRequested, this, &SettingsDialog::factoryResetRequested);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (!m_panel) return;
        m_panel->applyAndSave();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool SettingsDialog::settingsChanged() const {
    return m_panel && m_panel->settingsChanged();
}

int SettingsDialog::selectedLanguageIndex() const {
    return m_panel ? m_panel->selectedLanguageIndex() : 0;
}

QString SettingsDialog::modelPath() const {
    return m_panel ? m_panel->modelPath() : QString();
}

bool SettingsDialog::backgroundAutoAnalysis() const {
    return m_panel && m_panel->backgroundAutoAnalysis();
}

bool SettingsDialog::systemFileBypassFilter() const {
    return m_panel && m_panel->systemFileBypassFilter();
}

int SettingsDialog::coldArchiveYears() const
{
    return m_panel ? m_panel->coldArchiveYears() : 0;
}

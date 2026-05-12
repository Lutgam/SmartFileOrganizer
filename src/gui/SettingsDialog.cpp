#include "SettingsDialog.h"

#include "LanguageManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
static constexpr const char *kSettingsModelPathKey = "ai/model_path";
static constexpr const char *kSettingsBgAutoAnalyze = "workspace/background_auto_analysis";
static constexpr const char *kSettingsSystemFileBypass = "workspace/system_file_bypass_filter";
static constexpr const char *kSettingsColdArchiveYears = "workspace/cold_archive_years";
} // namespace

SettingsDialog::SettingsDialog(const QString &currentRootPath, QWidget *parent)
    : QDialog(parent), m_rootPath(currentRootPath) {
    setWindowTitle(tr("⚙️ Settings"));
    resize(640, 420);

    auto *root = new QVBoxLayout(this);

    // Language row
    {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("Language / 語言"), this));
        m_languageCombo = new QComboBox(this);
        m_languageCombo->addItem(tr("繁體中文"));
        m_languageCombo->addItem(tr("English"));
        row->addWidget(m_languageCombo, 1);
        root->addLayout(row);
    }

    // Model path row
    {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("AI model (.gguf)"), this));
        m_modelPathEdit = new QLineEdit(this);
        m_modelPathEdit->setPlaceholderText(tr("Select a .gguf model file"));
        row->addWidget(m_modelPathEdit, 1);
        m_browseBtn = new QPushButton(tr("Browse..."), this);
        row->addWidget(m_browseBtn);
        root->addLayout(row);
    }

    {
        m_bgAutoAnalyze = new QCheckBox(tr("Enable background auto-analysis (debounced folder watch)"), this);
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
        row->addWidget(new QLabel(tr("Ignore & archive files not modified for"), this));
        m_coldArchiveCombo = new QComboBox(this);
        m_coldArchiveCombo->addItem(tr("Off (disabled)"), 0);
        m_coldArchiveCombo->addItem(tr("1 year"), 1);
        m_coldArchiveCombo->addItem(tr("3 years"), 3);
        m_coldArchiveCombo->addItem(tr("5 years"), 5);
        row->addWidget(m_coldArchiveCombo, 1);
        root->addLayout(row);
    }

    {
        auto *grp = new QGroupBox(tr("資料與快取管理"), this);
        auto *gv = new QVBoxLayout(grp);
        m_btnClearAi = new QPushButton(tr("清除 AI 分析快取（保留路徑與 Hash）"), grp);
        m_btnClearHash = new QPushButton(tr("清除雜湊紀錄（強制重新計算 SHA-256）"), grp);
        m_btnFactoryReset = new QPushButton(tr("徹底重置工作區（刪除 metadata）"), grp);
        gv->addWidget(m_btnClearAi);
        gv->addWidget(m_btnClearHash);
        gv->addWidget(m_btnFactoryReset);
        root->addWidget(grp);

        const bool allowData = !m_rootPath.trimmed().isEmpty();
        m_btnClearAi->setEnabled(allowData);
        m_btnClearHash->setEnabled(allowData);
        m_btnFactoryReset->setEnabled(allowData);

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

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    loadFromSettings();

    connect(m_browseBtn, &QPushButton::clicked, this, &SettingsDialog::browseModel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        saveToSettings();
        emit settingsApplied();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool SettingsDialog::settingsChanged() const {
    return m_changed;
}

int SettingsDialog::selectedLanguageIndex() const {
    return m_languageCombo ? m_languageCombo->currentIndex() : 0;
}

QString SettingsDialog::modelPath() const {
    return m_modelPathEdit ? m_modelPathEdit->text().trimmed() : QString();
}

bool SettingsDialog::backgroundAutoAnalysis() const {
    return m_bgAutoAnalyze && m_bgAutoAnalyze->isChecked();
}

bool SettingsDialog::systemFileBypassFilter() const {
    return m_systemFileBypass && m_systemFileBypass->isChecked();
}

int SettingsDialog::coldArchiveYears() const
{
    if (!m_coldArchiveCombo) return 0;
    return m_coldArchiveCombo->currentData().toInt();
}

void SettingsDialog::loadFromSettings() {
    QSettings s;
    // Language from LanguageManager (already persisted)
    const auto lang = LanguageManager::instance().language();
    if (m_languageCombo) {
        m_languageCombo->setCurrentIndex(lang == LanguageManager::Language::EN_US ? 1 : 0);
    }
    if (m_modelPathEdit) {
        m_modelPathEdit->setText(s.value(QString::fromLatin1(kSettingsModelPathKey)).toString());
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
}

void SettingsDialog::saveToSettings() {
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
}

void SettingsDialog::browseModel() {
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


#include "SettingsDialog.h"

#include "LanguageManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
static constexpr const char *kSettingsModelPathKey = "ai/model_path";
static constexpr const char *kSettingsBgAutoAnalyze = "workspace/background_auto_analysis";
} // namespace

SettingsDialog::SettingsDialog(const QString &currentRootPath, QWidget *parent)
    : QDialog(parent), m_rootPath(currentRootPath) {
    setWindowTitle(tr("⚙️ Settings"));
    resize(640, 280);

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


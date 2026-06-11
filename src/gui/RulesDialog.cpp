#include "RulesDialog.h"
#include "LanguageManager.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

RulesDialog::RulesDialog(const QString &workspaceRoot, QWidget *parent)
    : QDialog(parent), m_workspaceRoot(QDir::cleanPath(workspaceRoot))
{
    auto &lm = LanguageManager::instance();
    setWindowTitle(lm.getText(QStringLiteral("rules_dialog_title")));
    resize(720, 420);

    auto *root = new QVBoxLayout(this);

    auto *intro = new QLabel(lm.getText(QStringLiteral("rules_dialog_intro")), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({lm.getText(QStringLiteral("rules_col_name")),
                                        lm.getText(QStringLiteral("rules_col_condition")),
                                        lm.getText(QStringLiteral("rules_col_action")),
                                        lm.getText(QStringLiteral("rules_col_param")),
                                        lm.getText(QStringLiteral("rules_col_enabled"))});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    root->addWidget(m_table, 1);

    auto *btnRow = new QHBoxLayout();
    auto *btnAdd = new QPushButton(lm.getText(QStringLiteral("rules_btn_add")), this);
    connect(btnAdd, &QPushButton::clicked, this, &RulesDialog::addRuleInteractive);
    btnRow->addWidget(btnAdd);

    auto *btnRemove = new QPushButton(lm.getText(QStringLiteral("rules_btn_remove")), this);
    connect(btnRemove, &QPushButton::clicked, this, &RulesDialog::removeSelectedRule);
    btnRow->addWidget(btnRemove);

    auto *btnToggle = new QPushButton(lm.getText(QStringLiteral("rules_btn_toggle")), this);
    connect(btnToggle, &QPushButton::clicked, this, [this]() {
        const int row = m_table->currentRow();
        if (row < 0 || row >= m_rules.size())
            return;
        m_rules[row].enabled = !m_rules[row].enabled;
        persist();
        reloadTable();
        m_table->selectRow(row);
    });
    btnRow->addWidget(btnToggle);

    btnRow->addStretch(1);

    auto *btnApply = new QPushButton(lm.getText(QStringLiteral("rules_btn_apply")), this);
    btnApply->setDefault(true);
    connect(btnApply, &QPushButton::clicked, this, [this]() { emit applyRequested(m_rules); });
    btnRow->addWidget(btnApply);

    auto *btnClose = new QPushButton(lm.getText(QStringLiteral("rules_btn_close")), this);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(btnClose);

    root->addLayout(btnRow);

    m_rules = SfAutoRuleEngine::loadFromFile(rulesFilePath());
    reloadTable();
}

QString RulesDialog::rulesFilePath() const
{
    return QDir(m_workspaceRoot).absoluteFilePath(QStringLiteral(".smartfile/rules.json"));
}

void RulesDialog::persist()
{
    SfAutoRuleEngine::saveToFile(rulesFilePath(), m_rules);
}

void RulesDialog::reloadTable()
{
    auto &lm = LanguageManager::instance();
    m_table->setRowCount(m_rules.size());
    for (int i = 0; i < m_rules.size(); ++i) {
        const SfAutoRule &r = m_rules[i];
        QStringList cond;
        if (!r.folder.trimmed().isEmpty())
            cond << QStringLiteral("📁 %1").arg(QDir(m_workspaceRoot).relativeFilePath(r.folder));
        if (!r.suffixCsv.trimmed().isEmpty())
            cond << QStringLiteral(".%1").arg(r.suffixCsv);
        if (!r.nameContains.trimmed().isEmpty())
            cond << QStringLiteral("\"%1\"").arg(r.nameContains);
        if (cond.isEmpty())
            cond << lm.getText(QStringLiteral("rules_cond_any"));

        m_table->setItem(i, 0, new QTableWidgetItem(r.name));
        m_table->setItem(i, 1, new QTableWidgetItem(cond.join(QStringLiteral("  "))));
        m_table->setItem(i, 2,
                         new QTableWidgetItem(r.action == SfAutoRule::Action::MoveToFolder
                                                  ? lm.getText(QStringLiteral("rules_action_move"))
                                                  : lm.getText(QStringLiteral("rules_action_tag"))));
        m_table->setItem(i, 3, new QTableWidgetItem(r.actionParam));
        m_table->setItem(i, 4,
                         new QTableWidgetItem(r.enabled ? QStringLiteral("✅") : QStringLiteral("⏸")));
    }
}

void RulesDialog::addRuleInteractive()
{
    auto &lm = LanguageManager::instance();
    QDialog d(this);
    d.setWindowTitle(lm.getText(QStringLiteral("rules_add_title")));
    auto *form = new QFormLayout(&d);

    auto *nameEdit = new QLineEdit(&d);
    form->addRow(lm.getText(QStringLiteral("rules_col_name")), nameEdit);

    auto *folderRow = new QHBoxLayout();
    auto *folderEdit = new QLineEdit(&d);
    folderEdit->setPlaceholderText(lm.getText(QStringLiteral("rules_folder_placeholder")));
    auto *folderBtn = new QPushButton(QStringLiteral("…"), &d);
    QObject::connect(folderBtn, &QPushButton::clicked, &d, [&]() {
        const QString dir = QFileDialog::getExistingDirectory(&d, QString(), m_workspaceRoot);
        if (!dir.isEmpty())
            folderEdit->setText(QDir::cleanPath(dir));
    });
    folderRow->addWidget(folderEdit, 1);
    folderRow->addWidget(folderBtn);
    form->addRow(lm.getText(QStringLiteral("rules_field_folder")), folderRow);

    auto *suffixEdit = new QLineEdit(&d);
    suffixEdit->setPlaceholderText(QStringLiteral("pdf,docx,xlsx"));
    form->addRow(lm.getText(QStringLiteral("rules_field_suffix")), suffixEdit);

    auto *containsEdit = new QLineEdit(&d);
    containsEdit->setPlaceholderText(lm.getText(QStringLiteral("rules_contains_placeholder")));
    form->addRow(lm.getText(QStringLiteral("rules_field_contains")), containsEdit);

    auto *actionCombo = new QComboBox(&d);
    actionCombo->addItem(lm.getText(QStringLiteral("rules_action_tag")));
    actionCombo->addItem(lm.getText(QStringLiteral("rules_action_move")));
    form->addRow(lm.getText(QStringLiteral("rules_col_action")), actionCombo);

    auto *paramEdit = new QLineEdit(&d);
    paramEdit->setPlaceholderText(lm.getText(QStringLiteral("rules_param_placeholder")));
    form->addRow(lm.getText(QStringLiteral("rules_col_param")), paramEdit);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    form->addRow(bb);

    if (d.exec() != QDialog::Accepted)
        return;

    SfAutoRule r;
    r.name = nameEdit->text().trimmed();
    r.folder = folderEdit->text().trimmed();
    r.suffixCsv = suffixEdit->text().trimmed();
    r.nameContains = containsEdit->text().trimmed();
    r.action = actionCombo->currentIndex() == 1 ? SfAutoRule::Action::MoveToFolder
                                                : SfAutoRule::Action::AddTag;
    r.actionParam = paramEdit->text().trimmed();
    if (r.name.isEmpty() || r.actionParam.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Smartflie"),
                             lm.getText(QStringLiteral("rules_add_incomplete")));
        return;
    }
    m_rules.append(r);
    persist();
    reloadTable();
}

void RulesDialog::removeSelectedRule()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_rules.size())
        return;
    m_rules.removeAt(row);
    persist();
    reloadTable();
}

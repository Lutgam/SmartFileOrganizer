#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>

#include "core/AutoRuleEngine.h"

/// Unit tests for the deterministic auto-organize rule engine: matching
/// semantics, first-rule-wins, already-in-place skip, and persistence.
class TestAutoRules : public QObject
{
    Q_OBJECT

private slots:
    void suffixMatch();
    void nameContainsMatch();
    void folderScopedMatch();
    void disabledRuleNeverMatches();
    void firstMatchingRuleWins();
    void moveSkipsFilesAlreadyInPlace();
    void persistence_roundTrip();

private:
    QString touch(const QString &dir, const QString &rel) const
    {
        const QString p = dir + QLatin1Char('/') + rel;
        QDir().mkpath(QFileInfo(p).absolutePath());
        QFile f(p);
        if (!f.open(QIODevice::WriteOnly))
            return QString();
        f.write("x");
        return QDir::cleanPath(p);
    }
};

void TestAutoRules::suffixMatch()
{
    SfAutoRule r;
    r.name = QStringLiteral("PDF 歸檔");
    r.suffixCsv = QStringLiteral("pdf, docx");
    r.actionParam = QStringLiteral("文件");
    QVERIFY(SfAutoRuleEngine::ruleMatchesFile(r, QStringLiteral("/ws/a/report.PDF")));
    QVERIFY(SfAutoRuleEngine::ruleMatchesFile(r, QStringLiteral("/ws/b.docx")));
    QVERIFY(!SfAutoRuleEngine::ruleMatchesFile(r, QStringLiteral("/ws/c.txt")));
}

void TestAutoRules::nameContainsMatch()
{
    SfAutoRule r;
    r.name = QStringLiteral("發票");
    r.nameContains = QStringLiteral("發票");
    r.actionParam = QStringLiteral("財務");
    QVERIFY(SfAutoRuleEngine::ruleMatchesFile(r, QStringLiteral("/ws/2024_電子發票_03.pdf")));
    QVERIFY(!SfAutoRuleEngine::ruleMatchesFile(r, QStringLiteral("/ws/收據.pdf")));
}

void TestAutoRules::folderScopedMatch()
{
    SfAutoRule r;
    r.name = QStringLiteral("下載區");
    r.folder = QStringLiteral("/ws/Downloads");
    r.actionParam = QStringLiteral("待整理");
    QVERIFY(SfAutoRuleEngine::ruleMatchesFile(r, QStringLiteral("/ws/Downloads/a.txt")));
    QVERIFY(SfAutoRuleEngine::ruleMatchesFile(r, QStringLiteral("/ws/Downloads/sub/b.txt")));
    QVERIFY(!SfAutoRuleEngine::ruleMatchesFile(r, QStringLiteral("/ws/Other/a.txt")));
    // Prefix must respect path boundaries.
    QVERIFY(!SfAutoRuleEngine::ruleMatchesFile(r, QStringLiteral("/ws/DownloadsBackup/a.txt")));
}

void TestAutoRules::disabledRuleNeverMatches()
{
    SfAutoRule r;
    r.name = QStringLiteral("x");
    r.actionParam = QStringLiteral("y");
    r.enabled = false;
    QVERIFY(!SfAutoRuleEngine::ruleMatchesFile(r, QStringLiteral("/ws/a.txt")));
}

void TestAutoRules::firstMatchingRuleWins()
{
    QTemporaryDir dir;
    touch(dir.path(), QStringLiteral("invoice_2024.pdf"));

    SfAutoRule first;
    first.name = QStringLiteral("規則一");
    first.suffixCsv = QStringLiteral("pdf");
    first.actionParam = QStringLiteral("文件");

    SfAutoRule second;
    second.name = QStringLiteral("規則二");
    second.nameContains = QStringLiteral("invoice");
    second.actionParam = QStringLiteral("發票");

    const auto matches = SfAutoRuleEngine::findMatches({first, second}, dir.path());
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches[0].ruleIndex, 0);
}

void TestAutoRules::moveSkipsFilesAlreadyInPlace()
{
    QTemporaryDir dir;
    touch(dir.path(), QStringLiteral("財務/invoice.pdf"));

    SfAutoRule r;
    r.name = QStringLiteral("發票歸檔");
    r.suffixCsv = QStringLiteral("pdf");
    r.action = SfAutoRule::Action::MoveToFolder;
    r.actionParam = QStringLiteral("財務");

    const auto matches = SfAutoRuleEngine::findMatches({r}, dir.path());
    QVERIFY(matches.isEmpty());
}

void TestAutoRules::persistence_roundTrip()
{
    QTemporaryDir dir;
    const QString p = dir.path() + QStringLiteral("/rules.json");

    SfAutoRule r;
    r.name = QStringLiteral("螢幕截圖");
    r.nameContains = QStringLiteral("Screenshot");
    r.suffixCsv = QStringLiteral("png");
    r.action = SfAutoRule::Action::MoveToFolder;
    r.actionParam = QStringLiteral("截圖");
    r.enabled = true;

    QVERIFY(SfAutoRuleEngine::saveToFile(p, {r}));
    const auto loaded = SfAutoRuleEngine::loadFromFile(p);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded[0].name, r.name);
    QCOMPARE(loaded[0].nameContains, r.nameContains);
    QCOMPARE(loaded[0].suffixCsv, r.suffixCsv);
    QVERIFY(loaded[0].action == SfAutoRule::Action::MoveToFolder);
    QCOMPARE(loaded[0].actionParam, r.actionParam);
    QVERIFY(loaded[0].enabled);
}

QTEST_GUILESS_MAIN(TestAutoRules)
#include "test_autorules.moc"

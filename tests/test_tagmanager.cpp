#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "core/TagManager.h"

#include <QJsonArray>
#include <QJsonObject>

/// Unit tests for TagManager: dual-index consistency, normalization,
/// persistence round-trips, relocation and the hash-analysis cache.
class TestTagManager : public QObject
{
    Q_OBJECT

private slots:
    void stripAiPrefix_variants();
    void hasAiPrefix_variants();
    void addAndGetTags();
    void synonymNormalization();
    void removeTag_cleansBothIndexes();
    void renameTag_movesFiles();
    void deleteTag_removesEverywhere();
    void relocateFilePath_movesTagsAndHash();
    void contentHash_roundTrip();
    void hashAnalysisCache_roundTrip();
    void persistence_saveAndReload();
    void contextualRejectedTags();
    void tagParents_hierarchy();
};

void TestTagManager::stripAiPrefix_variants()
{
    QCOMPARE(TagManager::stripAiPrefix(QStringLiteral("[AI] 資料庫")), QStringLiteral("資料庫"));
    QCOMPARE(TagManager::stripAiPrefix(QStringLiteral("[ai]報稅")), QStringLiteral("報稅"));
    QCOMPARE(TagManager::stripAiPrefix(QStringLiteral("[ AI ] 旅遊")), QStringLiteral("旅遊"));
    QCOMPARE(TagManager::stripAiPrefix(QStringLiteral("純標籤")), QStringLiteral("純標籤"));
    QCOMPARE(TagManager::stripAiPrefix(QStringLiteral("  [AI]  含空白  ")), QStringLiteral("含空白"));
}

void TestTagManager::hasAiPrefix_variants()
{
    QVERIFY(TagManager::hasAiPrefix(QStringLiteral("[AI] x")));
    QVERIFY(TagManager::hasAiPrefix(QStringLiteral("[ai]x")));
    QVERIFY(!TagManager::hasAiPrefix(QStringLiteral("AI x")));
    QVERIFY(!TagManager::hasAiPrefix(QStringLiteral("x [AI]")));
}

void TestTagManager::addAndGetTags()
{
    QTemporaryDir dir;
    TagManager tm;
    tm.loadTags(dir.path().toStdString());

    const QString f = dir.path() + QStringLiteral("/a.txt");
    tm.addTag(f, QStringLiteral("專題簡報"), false);
    tm.addTag(f, QStringLiteral("期末報告"), false);

    const auto tags = tm.getTags(f);
    QCOMPARE(tags.size(), std::size_t(2));

    const auto files = tm.getFilesByTag(QStringLiteral("專題簡報"));
    QCOMPARE(files.size(), std::size_t(1));
    QCOMPARE(files[0], f);
}

void TestTagManager::synonymNormalization()
{
    QTemporaryDir dir;
    TagManager tm;
    tm.loadTags(dir.path().toStdString());

    const QString f = dir.path() + QStringLiteral("/db.sql");
    tm.addTag(f, QStringLiteral("database"), false);

    // Synonym map folds "database" into the canonical preset tag.
    const auto files = tm.getFilesByTag(QStringLiteral("資料庫"));
    QCOMPARE(files.size(), std::size_t(1));
}

void TestTagManager::removeTag_cleansBothIndexes()
{
    QTemporaryDir dir;
    TagManager tm;
    tm.loadTags(dir.path().toStdString());

    const QString f = dir.path() + QStringLiteral("/a.txt");
    tm.addTag(f, QStringLiteral("專題簡報"), false);
    tm.removeTag(f, QStringLiteral("專題簡報"));

    QVERIFY(tm.getTags(f).empty());
    QVERIFY(tm.getFilesByTag(QStringLiteral("專題簡報")).empty());
}

void TestTagManager::renameTag_movesFiles()
{
    QTemporaryDir dir;
    TagManager tm;
    tm.loadTags(dir.path().toStdString());

    const QString f1 = dir.path() + QStringLiteral("/a.txt");
    const QString f2 = dir.path() + QStringLiteral("/b.txt");
    tm.addTag(f1, QStringLiteral("舊標籤名"), false);
    tm.addTag(f2, QStringLiteral("舊標籤名"), false);

    tm.renameTag(QStringLiteral("舊標籤名"), QStringLiteral("新標籤名"));

    QVERIFY(tm.getFilesByTag(QStringLiteral("舊標籤名")).empty());
    QCOMPARE(tm.getFilesByTag(QStringLiteral("新標籤名")).size(), std::size_t(2));
}

void TestTagManager::deleteTag_removesEverywhere()
{
    QTemporaryDir dir;
    TagManager tm;
    tm.loadTags(dir.path().toStdString());

    const QString f = dir.path() + QStringLiteral("/a.txt");
    tm.addTag(f, QStringLiteral("待刪標籤"), false);
    tm.addTag(f, QStringLiteral("保留標籤"), false);

    tm.deleteTag(QStringLiteral("待刪標籤"));

    QVERIFY(tm.getFilesByTag(QStringLiteral("待刪標籤")).empty());
    QCOMPARE(tm.getTags(f).size(), std::size_t(1));
}

void TestTagManager::relocateFilePath_movesTagsAndHash()
{
    QTemporaryDir dir;
    TagManager tm;
    tm.loadTags(dir.path().toStdString());

    const QString oldPath = dir.path() + QStringLiteral("/old.txt");
    const QString newPath = dir.path() + QStringLiteral("/new.txt");
    tm.addTag(oldPath, QStringLiteral("搬移測試"), false);
    tm.setFileContentHash(oldPath, QStringLiteral("abc123"), false);

    tm.relocateFilePath(oldPath, newPath, false);

    QVERIFY(tm.getTags(oldPath).empty());
    QCOMPARE(tm.getTags(newPath).size(), std::size_t(1));
    QCOMPARE(tm.fileContentHash(newPath), QStringLiteral("abc123"));
    QVERIFY(tm.fileContentHash(oldPath).isEmpty());
}

void TestTagManager::contentHash_roundTrip()
{
    QTemporaryDir dir;
    TagManager tm;
    tm.loadTags(dir.path().toStdString());

    const QString f = dir.path() + QStringLiteral("/a.bin");
    QVERIFY(tm.fileContentHash(f).isEmpty());
    tm.setFileContentHash(f, QStringLiteral("deadbeef"), false);
    QCOMPARE(tm.fileContentHash(f), QStringLiteral("deadbeef"));
}

void TestTagManager::hashAnalysisCache_roundTrip()
{
    QTemporaryDir dir;
    TagManager tm;
    tm.loadTags(dir.path().toStdString());

    QJsonObject analysis;
    analysis.insert(QStringLiteral("summary"), QStringLiteral("一份資料庫期末考的解答。"));
    QJsonArray tags;
    tags.append(QStringLiteral("資料庫"));
    tags.append(QStringLiteral("期末考"));
    analysis.insert(QStringLiteral("tags"), tags);

    tm.recordHashAnalysis(QStringLiteral("hash1"), analysis, false);

    QJsonObject out;
    QVERIFY(tm.tryGetHashAnalysis(QStringLiteral("hash1"), &out));
    QCOMPARE(out.value(QStringLiteral("summary")).toString(), QStringLiteral("一份資料庫期末考的解答。"));
    QCOMPARE(out.value(QStringLiteral("tags")).toArray().size(), 2);

    QVERIFY(!tm.tryGetHashAnalysis(QStringLiteral("missing"), &out));
}

void TestTagManager::persistence_saveAndReload()
{
    QTemporaryDir dir;
    const QString f = dir.path() + QStringLiteral("/persist.txt");

    {
        TagManager tm;
        tm.loadTags(dir.path().toStdString());
        tm.addTag(f, QStringLiteral("持久化測試"), false);
        tm.setFileContentHash(f, QStringLiteral("cafef00d"), false);
        tm.saveTags();
    }

    TagManager tm2;
    tm2.loadTags(dir.path().toStdString());
    QCOMPARE(tm2.getTags(f).size(), std::size_t(1));
    QCOMPARE(tm2.fileContentHash(f), QStringLiteral("cafef00d"));
}

void TestTagManager::contextualRejectedTags()
{
    QTemporaryDir dir;
    TagManager tm;
    tm.loadTags(dir.path().toStdString());

    const QString f1 = dir.path() + QStringLiteral("/a.txt");
    const QString f2 = dir.path() + QStringLiteral("/b.txt");
    tm.addTag(f1, QStringLiteral("錯誤標籤"), false);
    tm.addContextualRejectedTag(f1, QStringLiteral("錯誤標籤"), true);

    // Rejected only for f1, not globally for f2.
    QVERIFY(tm.getRejectedTagsForFile(f1).contains(QStringLiteral("錯誤標籤")));
    QVERIFY(!tm.getRejectedTagsForFile(f2).contains(QStringLiteral("錯誤標籤")));
    // Tag removed from f1's metadata.
    QVERIFY(tm.getTags(f1).empty());
}

void TestTagManager::tagParents_hierarchy()
{
    QTemporaryDir dir;
    TagManager tm;
    tm.loadTags(dir.path().toStdString());

    const QString f = dir.path() + QStringLiteral("/a.txt");
    tm.addTag(f, QStringLiteral("[AI] 子標籤"), false);

    QVERIFY(tm.setAiTagParent(QStringLiteral("[AI] 子標籤"), QStringLiteral("[AI] 父分類"), false));
    QCOMPARE(tm.tagParent(QStringLiteral("[AI] 子標籤")), QStringLiteral("[AI] 父分類"));

    const auto children = tm.directChildTags(QStringLiteral("[AI] 父分類"));
    QCOMPARE(children.size(), std::size_t(1));
}

QTEST_GUILESS_MAIN(TestTagManager)
#include "test_tagmanager.moc"

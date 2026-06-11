#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "core/DrawerCategoryLut.h"

/// Unit tests for the drawer-category lookup table: builtin defaults,
/// keyword matching, key normalization and file round-trips.
class TestDrawerLut : public QObject
{
    Q_OBJECT

private slots:
    void builtinDefault_hasDrawers();
    void matchText_knownKeywords();
    void matchText_unknownFallsToMisc();
    void normalizeDrawerKey_canonical();
    void isSyntheticDrawerFolderTag_detection();
    void fileRoundTrip();
};

void TestDrawerLut::builtinDefault_hasDrawers()
{
    const SfDrawerCategoryLut lut = SfDrawerCategoryLut::builtinDefault();
    const QStringList keys = lut.drawerKeys();
    QVERIFY(keys.size() >= 5);
    QVERIFY(keys.contains(QStringLiteral("📦 雜項")));
    QVERIFY(keys.contains(QStringLiteral("🎓 大學學業與通識")));
}

void TestDrawerLut::matchText_knownKeywords()
{
    const SfDrawerCategoryLut lut = SfDrawerCategoryLut::builtinDefault();
    QCOMPARE(lut.matchText(QStringLiteral("期末考解答")), QStringLiteral("🎓 大學學業與通識"));
    QCOMPARE(lut.matchText(QStringLiteral("微積分")), QStringLiteral("🔬 STEM與醫學專業"));
    QCOMPARE(lut.matchText(QStringLiteral("pytorch training")), QStringLiteral("🤖 AI與資料科學"));
    QCOMPARE(lut.matchText(QStringLiteral("民法概要")), QStringLiteral("⚖️ 法商與人文社會"));
}

void TestDrawerLut::matchText_unknownFallsToMisc()
{
    const SfDrawerCategoryLut lut = SfDrawerCategoryLut::builtinDefault();
    QCOMPARE(lut.matchText(QStringLiteral("zzqq_xyzzy_nothing")), QStringLiteral("📦 雜項"));
}

void TestDrawerLut::normalizeDrawerKey_canonical()
{
    const SfDrawerCategoryLut lut = SfDrawerCategoryLut::builtinDefault();
    // Canonical keys map to themselves.
    for (const QString &k : lut.drawerKeys())
        QCOMPARE(lut.normalizeDrawerKey(k), k);
}

void TestDrawerLut::isSyntheticDrawerFolderTag_detection()
{
    const SfDrawerCategoryLut lut = SfDrawerCategoryLut::builtinDefault();
    QVERIFY(lut.isSyntheticDrawerFolderTag(QStringLiteral("[AI] 🎓 大學學業與通識")));
    QVERIFY(!lut.isSyntheticDrawerFolderTag(QStringLiteral("[AI] 資料庫")));
    // Misc drawer is intentionally excluded from synthetic-folder detection.
    QVERIFY(!lut.isSyntheticDrawerFolderTag(QStringLiteral("[AI] 📦 雜項")));
}

void TestDrawerLut::fileRoundTrip()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/categories_config.json");

    QVERIFY(SfDrawerCategoryLut::writeDefaultToFile(path));
    const SfDrawerCategoryLut loaded = SfDrawerCategoryLut::loadFromFile(path);
    const SfDrawerCategoryLut builtin = SfDrawerCategoryLut::builtinDefault();

    QCOMPARE(loaded.drawerKeys().size(), builtin.drawerKeys().size());
    QCOMPARE(loaded.matchText(QStringLiteral("期末考解答")), builtin.matchText(QStringLiteral("期末考解答")));
}

QTEST_GUILESS_MAIN(TestDrawerLut)
#include "test_drawerlut.moc"

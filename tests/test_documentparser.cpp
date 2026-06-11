#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFile>

#include "core/DocumentParser.h"

/// Unit tests for DocumentParser text extraction, sanitization and truncation.
class TestDocumentParser : public QObject
{
    Q_OBJECT

private slots:
    void extractText_plainTxt();
    void extractText_cjkContent();
    void extractTextForAi_txt();
    void truncateForAi_caps();
    void sanitizeTextForAi_keepsNormalText();
    void extractText_missingFile();

private:
    QString writeTempFile(const QString &dir, const QString &name, const QByteArray &bytes) const
    {
        const QString p = dir + QLatin1Char('/') + name;
        QFile f(p);
        if (!f.open(QIODevice::WriteOnly))
            return QString();
        f.write(bytes);
        f.close();
        return p;
    }
};

void TestDocumentParser::extractText_plainTxt()
{
    QTemporaryDir dir;
    const QString p = writeTempFile(dir.path(), QStringLiteral("plain.txt"),
                                    QByteArrayLiteral("hello smartfile organizer"));
    QVERIFY(!p.isEmpty());

    const QString out = DocumentParser::extractTextForAi(p);
    QVERIFY(out.contains(QStringLiteral("hello smartfile organizer")));
}

void TestDocumentParser::extractText_cjkContent()
{
    QTemporaryDir dir;
    const QString content = QStringLiteral("這是一份繁體中文的測試文件，包含資料庫期末考解答。");
    const QString p = writeTempFile(dir.path(), QStringLiteral("中文檔名測試.txt"), content.toUtf8());
    QVERIFY(!p.isEmpty());

    const QString out = DocumentParser::extractTextForAi(p);
    QVERIFY(out.contains(QStringLiteral("資料庫期末考解答")));
}

void TestDocumentParser::extractTextForAi_txt()
{
    QTemporaryDir dir;
    const QString content = QStringLiteral("AI 分析用的內文。");
    const QString p = writeTempFile(dir.path(), QStringLiteral("forai.txt"), content.toUtf8());
    QVERIFY(!p.isEmpty());

    bool pdfMetadataOnly = false;
    const QString out = DocumentParser::extractTextForAi(p, &pdfMetadataOnly);
    QVERIFY(out.contains(QStringLiteral("AI 分析用的內文")));
    QVERIFY(!pdfMetadataOnly);
}

void TestDocumentParser::truncateForAi_caps()
{
    const QString longText(DocumentParser::kAiTextMaxChars * 2, QLatin1Char('x'));
    const QString out = DocumentParser::truncateForAi(longText);
    QVERIFY(out.size() <= DocumentParser::kAiTextMaxChars + 64); // allow ellipsis marker slack
}

void TestDocumentParser::sanitizeTextForAi_keepsNormalText()
{
    const QString in = QStringLiteral("正常文字 normal text 123");
    const QString out = DocumentParser::sanitizeTextForAi(in);
    QVERIFY(out.contains(QStringLiteral("正常文字")));
    QVERIFY(out.contains(QStringLiteral("normal text")));
}

void TestDocumentParser::extractText_missingFile()
{
    const QString out = DocumentParser::extractTextQString(QStringLiteral("/nonexistent/no_such_file.txt"));
    QVERIFY(out.trimmed().isEmpty());
}

QTEST_GUILESS_MAIN(TestDocumentParser)
#include "test_documentparser.moc"

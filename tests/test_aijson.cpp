#include <QtTest/QtTest>

#include "ai/AiJsonSanitizer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace SfAiJson;

/// Unit tests for the LLM output safety-net layer: fence stripping, JSON
/// extraction, tag sanitization (cap / dedup / reject filter) and the
/// guaranteed-valid-JSON contract of ensureSuggestTagsJson.
class TestAiJson : public QObject
{
    Q_OBJECT

private slots:
    void stripFences_markdown();
    void brutalExtract_sliceObject();
    void ensure_validJsonPassesThrough();
    void ensure_fencedJsonAccepted();
    void ensure_proseWrappedJsonAccepted();
    void ensure_emptyInputFailsafe();
    void ensure_errorInputFailsafe();
    void ensure_cancelledPassesThrough();
    void ensure_alwaysReturnsValidJson();
    void tags_cappedAtThree();
    void tags_longTagsDropped();
    void tags_rejectedFiltered();
    void tags_overlapDeduped();
    void summary_nestedJsonUnwrapped();

private:
    QJsonObject parse(const std::string &s) const
    {
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(s));
        return doc.object();
    }
};

void TestAiJson::stripFences_markdown()
{
    QCOMPARE(stripMarkdownJsonFences(QStringLiteral("```json\n{\"a\":1}\n```")), QStringLiteral("{\"a\":1}"));
    QCOMPARE(stripMarkdownJsonFences(QStringLiteral("```\n{\"a\":1}\n```")), QStringLiteral("{\"a\":1}"));
    QCOMPARE(stripMarkdownJsonFences(QStringLiteral("{\"a\":1}")), QStringLiteral("{\"a\":1}"));
}

void TestAiJson::brutalExtract_sliceObject()
{
    const QString out = brutalExtractJsonFromLlmResponse(
        QStringLiteral("Sure! Here is the JSON: {\"summary\":\"x\",\"tags\":[\"y\"]} Hope it helps."));
    QCOMPARE(out, QStringLiteral("{\"summary\":\"x\",\"tags\":[\"y\"]}"));
}

void TestAiJson::ensure_validJsonPassesThrough()
{
    const std::string out = ensureSuggestTagsJson(
        R"({"summary":"資料庫期末考的解答。","tags":["資料庫","期末考"]})", "");
    const QJsonObject obj = parse(out);
    QCOMPARE(obj.value(QStringLiteral("summary")).toString(), QStringLiteral("資料庫期末考的解答。"));
    QCOMPARE(obj.value(QStringLiteral("tags")).toArray().size(), 2);
}

void TestAiJson::ensure_fencedJsonAccepted()
{
    const std::string out = ensureSuggestTagsJson(
        "```json\n{\"summary\":\"報告摘要。\",\"tags\":[\"報告\"]}\n```", "");
    const QJsonObject obj = parse(out);
    QCOMPARE(obj.value(QStringLiteral("summary")).toString(), QStringLiteral("報告摘要。"));
}

void TestAiJson::ensure_proseWrappedJsonAccepted()
{
    const std::string out = ensureSuggestTagsJson(
        "Here is my analysis:\n{\"summary\":\"旅遊機票。\",\"tags\":[\"機票\"]}\nLet me know!", "");
    const QJsonObject obj = parse(out);
    QCOMPARE(obj.value(QStringLiteral("summary")).toString(), QStringLiteral("旅遊機票。"));
}

void TestAiJson::ensure_emptyInputFailsafe()
{
    const std::string out = ensureSuggestTagsJson("", "");
    const QJsonObject obj = parse(out);
    QVERIFY(!obj.value(QStringLiteral("summary")).toString().isEmpty());
    QVERIFY(!obj.value(QStringLiteral("tags")).toArray().isEmpty());
}

void TestAiJson::ensure_errorInputFailsafe()
{
    const std::string out = ensureSuggestTagsJson("Error: Model not loaded", "");
    const QJsonObject obj = parse(out);
    QVERIFY(!obj.value(QStringLiteral("summary")).toString().isEmpty());
}

void TestAiJson::ensure_cancelledPassesThrough()
{
    const std::string raw = "Error: Cancelled by user";
    QCOMPARE(ensureSuggestTagsJson(raw, ""), raw);
}

void TestAiJson::ensure_alwaysReturnsValidJson()
{
    const std::vector<std::string> inputs = {
        "complete garbage with no braces",
        "{broken json",
        "}{",
        "{\"no_summary_key\": 1}",
        "{{{{",
        "\xE4\xB8\xAD\xE6\x96\x87\xE4\xBA\x82\xE6\x96\x87", // 中文亂文
    };
    for (const std::string &in : inputs) {
        const std::string out = ensureSuggestTagsJson(in, "");
        if (QString::fromStdString(in).startsWith(QStringLiteral("Error: Cancelled")))
            continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(out), &err);
        QVERIFY2(err.error == QJsonParseError::NoError && doc.isObject(),
                 qPrintable(QStringLiteral("input '%1' produced invalid JSON")
                                .arg(QString::fromStdString(in))));
        QVERIFY(doc.object().contains(QStringLiteral("summary")));
        QVERIFY(doc.object().contains(QStringLiteral("tags")));
    }
}

void TestAiJson::tags_cappedAtThree()
{
    const std::string out = ensureSuggestTagsJson(
        R"({"summary":"s","tags":["甲一","乙二","丙三","丁四","戊五"]})", "");
    QVERIFY(parse(out).value(QStringLiteral("tags")).toArray().size() <= 3);
}

void TestAiJson::tags_longTagsDropped()
{
    const std::string out = ensureSuggestTagsJson(
        R"({"summary":"s","tags":["這是一個超過十五個字元長度上限的超長標籤名稱","短標"]})", "");
    const QJsonArray tags = parse(out).value(QStringLiteral("tags")).toArray();
    for (const QJsonValue &v : tags)
        QVERIFY(v.toString().size() <= 15);
}

void TestAiJson::tags_rejectedFiltered()
{
    const std::string out = ensureSuggestTagsJson(
        R"({"summary":"s","tags":["壞標籤","好標籤"]})", "壞標籤");
    const QJsonArray tags = parse(out).value(QStringLiteral("tags")).toArray();
    for (const QJsonValue &v : tags)
        QVERIFY(v.toString() != QStringLiteral("壞標籤"));
}

void TestAiJson::tags_overlapDeduped()
{
    // "資料庫" and "資料庫檔案" share the dedup key "資料庫" → only one kept.
    const std::string out = ensureSuggestTagsJson(
        R"({"summary":"s","tags":["資料庫","資料庫檔案"]})", "");
    QCOMPARE(parse(out).value(QStringLiteral("tags")).toArray().size(), 1);
}

void TestAiJson::summary_nestedJsonUnwrapped()
{
    // LLM sometimes nests the whole JSON inside the summary string.
    const QString coerced = coerceSummaryText(
        QStringLiteral("{\"summary\":\"內層真正的摘要。\",\"tags\":[\"x\"]}"));
    QCOMPARE(coerced, QStringLiteral("內層真正的摘要。"));
}

QTEST_GUILESS_MAIN(TestAiJson)
#include "test_aijson.moc"

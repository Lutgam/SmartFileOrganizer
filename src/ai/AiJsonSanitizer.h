#ifndef AIJSONSANITIZER_H
#define AIJSONSANITIZER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <string>

/// Post-processing / safety-net layer for LLM suggest-tags output.
/// Pure functions (QtCore + LanguageManager only) so they are unit-testable
/// without linking llama.cpp.
namespace SfAiJson {

QString analysisNonstandardFormatFallbackJson();
QString analysisParsedNoSummaryText();
std::string suggestTagsFailsafeJson();

QString stripMarkdownJsonFences(QString text);
/// Brutal slice: first `{` through last `}`; if none, return compact fallback JSON for Qt to parse.
QString brutalExtractJsonFromLlmResponse(QString text);

QString aiTagDedupKey(QString tag);
QStringList parseRejectedTagsCsv(const std::string &rejectedTagsCsv);
bool isTagOnRejectedList(const QString &tag, const QStringList &rejectedTags);

QJsonArray sanitizeTagsArray(const QJsonArray &arr, const QStringList &rejectedTags = {},
                             int maxTags = 3, int maxLen = 15);
QString coerceSummaryText(QString summary);
QJsonObject sanitizeSuggestTagsObject(const QJsonObject &obj, const QStringList &rejectedTags);

/// Wrap raw (non-JSON) LLM text as a valid suggest-tags JSON document.
std::string jsonFromUnblockedRawOutput(QString cleanOutput);

/// Main entry: take raw LLM output, return guaranteed-valid suggest-tags JSON
/// ({"summary": ..., "tags": [...]}) with rejected tags filtered out.
std::string ensureSuggestTagsJson(const std::string &raw, const std::string &rejectedTagsCsv);

} // namespace SfAiJson

#endif // AIJSONSANITIZER_H

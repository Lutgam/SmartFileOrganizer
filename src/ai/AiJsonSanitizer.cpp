#include "AiJsonSanitizer.h"

#include "../core/TagManager.h"
#include "../gui/LanguageManager.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

namespace SfAiJson {

namespace {
constexpr const char kSuggestTagsFallbackJson[] =
    R"({"summary":"[系統提示：檔案內容過於複雜或包含特殊編碼，已切換至安全模式讀取檔名進行智能分類。]","tags":["通用文件"]})";
} // namespace

QString analysisNonstandardFormatFallbackJson()
{
  auto &lm = LanguageManager::instance();
  QJsonObject o;
  o.insert(QStringLiteral("summary"), lm.getText(QStringLiteral("analysis_nonstandard_format_summary")));
  QJsonArray ta;
  ta.append(lm.getText(QStringLiteral("analysis_manual_classify_tag")));
  o.insert(QStringLiteral("tags"), ta);
  return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString analysisParsedNoSummaryText()
{
  return LanguageManager::instance().getText(QStringLiteral("analysis_parsed_no_summary"));
}

std::string suggestTagsFailsafeJson()
{
  const QString fallbackJson = QString::fromUtf8(kSuggestTagsFallbackJson);
  const QJsonDocument doc = QJsonDocument::fromJson(fallbackJson.toUtf8());
  return doc.toJson(QJsonDocument::Compact).toStdString();
}

QString stripMarkdownJsonFences(QString text)
{
  text = text.trimmed();
  text.remove(QRegularExpression(QStringLiteral("^```\\s*json\\s*"), QRegularExpression::CaseInsensitiveOption));
  text.remove(QRegularExpression(QStringLiteral("^```\\s*")));
  text.remove(QRegularExpression(QStringLiteral("```\\s*$")));
  return text.trimmed();
}

QString brutalExtractJsonFromLlmResponse(QString text)
{
  text = stripMarkdownJsonFences(text.trimmed());
  const int startIndex = text.indexOf(QLatin1Char('{'));
  const int endIndex = text.lastIndexOf(QLatin1Char('}'));
  if (startIndex != -1 && endIndex != -1 && endIndex >= startIndex)
    return text.mid(startIndex, endIndex - startIndex + 1);
  return analysisNonstandardFormatFallbackJson();
}

QString aiTagDedupKey(QString tag)
{
  tag = tag.trimmed().toLower();
  static const QRegularExpression noiseSuffix(
      QStringLiteral("(檔案|文件|內容|設定檔|設定|變數|配置|資料)+$"));
  tag.remove(noiseSuffix);
  tag.remove(QRegularExpression(QStringLiteral("\\.(txt|json|xml|yaml|yml|ini|cfg|conf)$"),
                               QRegularExpression::CaseInsensitiveOption));
  return tag.trimmed();
}

QStringList parseRejectedTagsCsv(const std::string &rejectedTagsCsv)
{
  QStringList out;
  for (QString part : QString::fromStdString(rejectedTagsCsv).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    part = TagManager::stripAiPrefix(part.trimmed());
    if (!part.isEmpty()) out << part;
  }
  return out;
}

bool isTagOnRejectedList(const QString &tag, const QStringList &rejectedTags)
{
  if (rejectedTags.isEmpty()) return false;
  const QString norm = TagManager::stripAiPrefix(tag).trimmed();
  if (norm.isEmpty()) return false;
  const QString key = aiTagDedupKey(norm);
  for (const QString &rejected : rejectedTags) {
    const QString rejNorm = TagManager::stripAiPrefix(rejected).trimmed();
    if (rejNorm.isEmpty()) continue;
    if (norm.compare(rejNorm, Qt::CaseInsensitive) == 0) return true;
    const QString rejKey = aiTagDedupKey(rejNorm);
    if (!key.isEmpty() && !rejKey.isEmpty()
        && (key == rejKey || key.contains(rejKey) || rejKey.contains(key))) {
      return true;
    }
  }
  return false;
}

QJsonArray sanitizeTagsArray(const QJsonArray &arr, const QStringList &rejectedTags,
                             int maxTags, int maxLen)
{
  QJsonArray out;
  QSet<QString> seenKeys;
  for (const QJsonValue &v : arr) {
    if (out.size() >= maxTags) break;

    QString t = v.toString().trimmed();
    if (t.isEmpty() || t.compare(QStringLiteral("ai"), Qt::CaseInsensitive) == 0)
      continue;
    if (isTagOnRejectedList(t, rejectedTags))
      continue;
    if (t.size() > maxLen)
      continue;

    const QString key = aiTagDedupKey(t);
    if (key.isEmpty() || seenKeys.contains(key))
      continue;

    bool overlaps = false;
    for (int i = 0; i < out.size(); ++i) {
      const QString existingKey = aiTagDedupKey(out.at(i).toString());
      if (existingKey.isEmpty())
        continue;
      if (key == existingKey || key.contains(existingKey) || existingKey.contains(key)) {
        overlaps = true;
        break;
      }
    }
    if (overlaps)
      continue;

    seenKeys.insert(key);
    out.append(t);
  }

  if (out.isEmpty())
    out.append(LanguageManager::instance().getText(QStringLiteral("analysis_manual_classify_tag")));
  return out;
}

QString coerceSummaryText(QString summary)
{
  summary = summary.trimmed();
  if (summary.isEmpty() || summary == QStringLiteral("..."))
    return analysisParsedNoSummaryText();

  if (summary.startsWith(QLatin1Char('{')) || summary.contains(QStringLiteral("\"summary\""))) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(summary.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
      const QString inner = doc.object().value(QStringLiteral("summary")).toString().trimmed();
      if (!inner.isEmpty())
        summary = inner;
    }
  }

  if (summary.size() > 500)
    summary = summary.left(500).trimmed();
  if (summary.isEmpty() || summary == QStringLiteral("..."))
    summary = analysisParsedNoSummaryText();
  return summary;
}

QJsonObject sanitizeSuggestTagsObject(const QJsonObject &obj, const QStringList &rejectedTags)
{
  QJsonObject out;
  QString summary = coerceSummaryText(obj.value(QStringLiteral("summary")).toString());
  if (summary.isEmpty() || summary == QStringLiteral("..."))
    summary = analysisParsedNoSummaryText();
  out.insert(QStringLiteral("summary"), summary);
  out.insert(QStringLiteral("tags"), sanitizeTagsArray(obj.value(QStringLiteral("tags")).toArray(), rejectedTags));
  return out;
}

std::string jsonFromUnblockedRawOutput(QString cleanOutput)
{
  cleanOutput = stripMarkdownJsonFences(cleanOutput).trimmed();
  if (cleanOutput.isEmpty()) {
    cleanOutput = QStringLiteral(
        "[系統提示：檔案內容過於複雜或包含特殊編碼，已切換至安全模式讀取檔名進行智能分類。]");
  }

  QJsonObject fallbackObj;
  fallbackObj.insert(QStringLiteral("summary"), cleanOutput);
  QJsonArray fallbackTags;
  fallbackTags.append(QStringLiteral("智能解析摘要"));
  fallbackObj.insert(QStringLiteral("tags"), fallbackTags);

  return QJsonDocument(fallbackObj).toJson(QJsonDocument::Compact).toStdString();
}

std::string ensureSuggestTagsJson(const std::string &raw, const std::string &rejectedTagsCsv)
{
  const QStringList rejectedTags = parseRejectedTagsCsv(rejectedTagsCsv);
  const QString llmOutputString = QString::fromStdString(raw).trimmed();
  if (llmOutputString.isEmpty())
    return suggestTagsFailsafeJson();
  if (llmOutputString.startsWith(QStringLiteral("Error:"), Qt::CaseInsensitive)) {
    if (llmOutputString.startsWith(QStringLiteral("Error: Cancelled"), Qt::CaseInsensitive))
      return raw;
    return suggestTagsFailsafeJson();
  }

  QString rawResponse = brutalExtractJsonFromLlmResponse(llmOutputString);

  QJsonParseError parseError{};
  QJsonDocument doc = QJsonDocument::fromJson(rawResponse.toUtf8(), &parseError);

  if (parseError.error == QJsonParseError::NoError && doc.isObject()
      && doc.object().contains(QStringLiteral("summary"))) {
    return QJsonDocument(sanitizeSuggestTagsObject(doc.object(), rejectedTags))
        .toJson(QJsonDocument::Compact)
        .toStdString();
  }

  static const QRegularExpression re(QStringLiteral("\\{.*?\\}"));
  QRegularExpression jsonBlockRe(re);
  jsonBlockRe.setPatternOptions(QRegularExpression::DotMatchesEverythingOption);
  const QString stripped = stripMarkdownJsonFences(llmOutputString);
  auto it = jsonBlockRe.globalMatch(stripped);
  while (it.hasNext()) {
    const QString block = brutalExtractJsonFromLlmResponse(it.next().captured(0).trimmed());
    if (block.isEmpty())
      continue;
    QJsonParseError blockError{};
    const QJsonDocument blockDoc = QJsonDocument::fromJson(block.toUtf8(), &blockError);
    if (blockError.error != QJsonParseError::NoError || !blockDoc.isObject())
      continue;
    if (!blockDoc.object().contains(QStringLiteral("summary")))
      continue;
    return QJsonDocument(sanitizeSuggestTagsObject(blockDoc.object(), rejectedTags))
        .toJson(QJsonDocument::Compact)
        .toStdString();
  }

  return jsonFromUnblockedRawOutput(stripped);
}

} // namespace SfAiJson

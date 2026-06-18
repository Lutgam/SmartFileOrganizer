#ifndef DOCUMENTPARSER_H
#define DOCUMENTPARSER_H

#include <string>
#include <QString>

class DocumentParser
{
public:
    static constexpr int kAiPdfMaxPages = 5;
    static constexpr int kAiTextMaxChars = 1800;

    static std::string extractText(const std::string& filePath);
    /// Prefer this for paths with non-Latin characters (uses Qt I/O end-to-end).
    static QString extractTextQString(const QString& filePath);
    static QString extractPdfText(const QString& filePath);
    /// Text for LLM analysis: PDF/office routing, page/length limits, metadata fallback.
    static QString extractTextForAi(const QString& filePath, bool *pdfMetadataOnly = nullptr);
    static QString sanitizeTextForAi(const QString& text);
    static QString truncateForAi(const QString& text, int maxChars = kAiTextMaxChars);

    /// Metadata-only descriptor for files whose content cannot be read (images,
    /// binaries, unknown types): filename, extension, MIME type, size, modified
    /// date and—for images—pixel dimensions. Used so EVERY analyzable file can be
    /// classified, even without an extractable text layer.
    static QString extractMetadataContext(const QString& filePath);

private:
    static std::string parsDocx(const std::string& filePath);
    static std::string parseXlsx(const std::string& filePath);
    static std::string parsePdf(const std::string& filePath);
};

#endif // DOCUMENTPARSER_H

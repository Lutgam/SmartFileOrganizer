#include "DocumentParser.h"
#include "miniz.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QStringList>
#include <filesystem>
#include <iostream>
#include <algorithm>

#if defined(HAVE_QT_PDF)
#include <QPdfDocument>
#endif

#if defined(HAVE_POPPLER_QT6)
#include <poppler-qt6.h>
#endif

namespace fs = std::filesystem;

// Forward decl: helper defined below
static std::string extractZipEntry(const std::string& zipPath, const std::string& entryName);

std::string DocumentParser::extractText(const std::string& filePath)
{
    fs::path p(filePath);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Legacy binary formats: never try to unzip/parse.
    if (ext == ".doc" || ext == ".xls" || ext == ".ppt") {
        return "";
    }

    if (ext == ".docx" || ext == ".docm") {
        return parsDocx(filePath);
    } else if (ext == ".xlsx" || ext == ".xlsm") {
        return parseXlsx(filePath);
    } else if (ext == ".pptx" || ext == ".pptm") {
        try {
            QFile file(QString::fromStdString(filePath));
            if (!file.open(QIODevice::ReadOnly)) return "";
            const QByteArray fileData = file.readAll();
            file.close();
            if (fileData.isEmpty()) return "";

            mz_zip_archive zip_archive;
            memset(&zip_archive, 0, sizeof(zip_archive));
            if (!mz_zip_reader_init_mem(&zip_archive, fileData.constData(), fileData.size(), 0)) {
                return "";
            }

            QString text;
            static const QRegularExpression atRe(
                QStringLiteral("<[a-zA-Z0-9]+:t[^>]*>(.*?)</[a-zA-Z0-9]+:t>"),
                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
            const int num_files = mz_zip_reader_get_num_files(&zip_archive);
            for (int i = 0; i < num_files; ++i) {
                mz_zip_archive_file_stat st{};
                if (!mz_zip_reader_file_stat(&zip_archive, i, &st)) continue;
                const QString name = QString::fromUtf8(st.m_filename);
                if (!name.startsWith(QStringLiteral("ppt/slides/slide")) || !name.endsWith(QStringLiteral(".xml"))) continue;

                size_t file_size = 0;
                void *pHeap = mz_zip_reader_extract_to_heap(&zip_archive, i, &file_size, 0);
                if (!pHeap || file_size == 0) {
                    if (pHeap) mz_free(pHeap);
                    continue;
                }
                const QString xmlContent = QString::fromUtf8(static_cast<const char *>(pHeap), static_cast<int>(file_size));
                mz_free(pHeap);

                auto git = atRe.globalMatch(xmlContent);
                while (git.hasNext()) {
                    const auto m = git.next();
                    if (!m.hasMatch()) continue;
                    const QString chunk = m.captured(1).trimmed();
                    if (!chunk.isEmpty()) {
                        text += chunk;
                        text += QLatin1Char(' ');
                    }
                    if (text.size() > 8000) break;
                }
                if (text.size() > 8000) break;
            }

            mz_zip_reader_end(&zip_archive);
            return text.trimmed().toStdString();
        } catch (const std::exception &) {
            return "";
        } catch (...) {
            return "";
        }
    } else if (ext == ".odt" || ext == ".ods" || ext == ".odp") {
        // ODF formats store main content in content.xml
        std::string xmlContent = extractZipEntry(filePath, "content.xml");
        if (xmlContent.rfind("DEBUG:", 0) == 0) return "";
        if (xmlContent.empty()) return "";

        QString text;
        QXmlStreamReader xml(QString::fromStdString(xmlContent));
        while (!xml.atEnd() && !xml.hasError()) {
            const auto token = xml.readNext();
            if (token == QXmlStreamReader::StartElement) {
                const QString n = xml.name().toString();
                if (n == QStringLiteral("p")) {
                    text += QStringLiteral("\n");
                }
            } else if (token == QXmlStreamReader::Characters && !xml.isWhitespace()) {
                text += xml.text().toString();
                text += QStringLiteral(" ");
            }
            if (text.size() > 4000) break;
        }
        return text.trimmed().toStdString();
    } else if (ext == ".pdf") {
        return extractPdfText(QString::fromStdString(filePath)).toStdString();
    }
    return "";
}



// Helper to unzip specific file from archive to string
static std::string extractZipEntry(const std::string& zipPath, const std::string& entryName) {
    // Use QFile to handle Unicode paths on Windows correctly
    QFile file(QString::fromStdString(zipPath));
    if (!file.open(QIODevice::ReadOnly)) {
        return "DEBUG: Failed to open file via Qt (Unicode check): " + zipPath;
    }
    QByteArray fileData = file.readAll();
    file.close();

    if (fileData.isEmpty()) {
        return "DEBUG: File is empty: " + zipPath;
    }

    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    // Initialize zip reader from memory buffer instead of file path
    if (!mz_zip_reader_init_mem(&zip_archive, fileData.constData(), fileData.size(), 0)) {
        return "DEBUG: Failed to parse zip structure from memory: " + zipPath;
    }

    int file_index = mz_zip_reader_locate_file(&zip_archive, entryName.c_str(), NULL, 0);
    if (file_index < 0) {
        std::string msg = "DEBUG: Entry '" + entryName + "' not found. Files:\n";
        int num_files = mz_zip_reader_get_num_files(&zip_archive);
        for (int i = 0; i < num_files; i++) {
           mz_zip_archive_file_stat file_stat;
           if (mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
               msg += " - " + std::string(file_stat.m_filename) + "\n";
           }
        }
        mz_zip_reader_end(&zip_archive);
        return msg;
    }

    size_t file_size;
    void* p = mz_zip_reader_extract_file_to_heap(&zip_archive, entryName.c_str(), &file_size, 0);
    if (!p) {
        mz_zip_reader_end(&zip_archive);
        return "DEBUG: Failed to extract entry to heap.";
    }

    std::string content((char*)p, file_size);
    mz_free(p);
    mz_zip_reader_end(&zip_archive);
    return content;
}

std::string DocumentParser::parsDocx(const std::string& filePath)
{
    std::string xmlContent = extractZipEntry(filePath, "word/document.xml");
    if (xmlContent.rfind("DEBUG:", 0) == 0) return xmlContent; // Pass error
    if (xmlContent.empty()) return "DEBUG: Empty document.xml extracted";

    QString text;
    QXmlStreamReader xml(QString::fromStdString(xmlContent));

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name().toString() == "t") { // Text node in DOCX
                 text += xml.readElementText() + " ";
            }
            if (xml.name().toString() == "p") { // Paragraph
                text += "\n";
            }
        }
    }
    return text.trimmed().toStdString();
}

std::string DocumentParser::parseXlsx(const std::string& filePath)
{
    std::string sharedStringsXml = extractZipEntry(filePath, "xl/sharedStrings.xml");
    if (sharedStringsXml.rfind("DEBUG:", 0) == 0) {
        // Shared strings might just be missing, not an error.
        // But if it says "Failed to open zip", that is an error.
        if (sharedStringsXml.find("Failed to open zip") != std::string::npos) return sharedStringsXml;
        // else fallback to reading sheet1 directly if sharedStrings missing
        sharedStringsXml = ""; 
    }
    
    std::vector<QString> stringTable; // Index -> String

    if (!sharedStringsXml.empty()) {
        QXmlStreamReader xml(QString::fromStdString(sharedStringsXml));
        while (!xml.atEnd()) {
            if (xml.readNextStartElement()) {
                if (xml.name().toString() == "t") {
                     stringTable.push_back(xml.readElementText());
                }
            }
        }
    }

    // Read Sheet 1 (simplify to just sheet1 for now)
    std::string sheetXml = extractZipEntry(filePath, "xl/worksheets/sheet1.xml");
    if (sheetXml.rfind("DEBUG:", 0) == 0) return sheetXml; // Return failure if zip read failed
    if (sheetXml.empty()) return ""; 

    QString result;
    QXmlStreamReader xml(QString::fromStdString(sheetXml));
    while (!xml.atEnd()) {
         if (xml.readNextStartElement()) {
             if (xml.name().toString() == "c") { // Cell
                 QString type = xml.attributes().value("t").toString(); // 's' for shared string
                 
                 // Find value 'v' inside 'c'
                 // But 'c' might just contain 'v' node
                 // We need to read until end of 'c' or find 'v'
                 // Simplified:
             }
             if (xml.name().toString() == "v") { // Value
                 QString val = xml.readElementText();
                 // If previous cell type was 's', lookup in table
                 // Complex to track state here. 
                 // For simple search/AI, dumping sharedStrings is often enough!
             }
         }
    }
    
    // Combining shared strings is usually enough to capture the "text content" for AI analysis
    QString fullText;
    for(const auto& s : stringTable) {
        fullText += s + "\n";
    }
    
    // Fallback: if no shared strings, maybe inline strings?
    if (fullText.isEmpty()) {
        return "(XLSX Read: No shared strings found, raw numeric data skipped)";
    }
    
    return fullText.toStdString();
}

std::string DocumentParser::parsePdf(const std::string& filePath)
{
    QFileInfo fileInfo(QString::fromStdString(filePath));
    QString metadata = QString("檔名: %1, 建立於: %2, 大小: %3 bytes")
        .arg(fileInfo.fileName())
        .arg(fileInfo.birthTime().toString("yyyy-MM-dd"))
        .arg(fileInfo.size());
    return metadata.toStdString();
}

QString DocumentParser::extractPdfText(const QString& filePath)
{
    const QString clean = QFileInfo(filePath).absoluteFilePath();

#if defined(HAVE_QT_PDF)
    QPdfDocument doc;
    const auto err = doc.load(clean);
    if (err != QPdfDocument::Error::None) {
        return QString();
    }

    const int pages = doc.pageCount();
    QString out;
    for (int i = 0; i < pages; ++i) {
        const QSizeF sz = doc.pagePointSize(i);
        if (sz.width() <= 0 || sz.height() <= 0) continue;
        const QPdfSelection sel = doc.getSelection(i, QPointF(0, 0), QPointF(sz.width(), sz.height()));
        const QString pageText = sel.text();
        if (!pageText.trimmed().isEmpty()) {
            out += pageText;
            out += QStringLiteral("\n");
        }
        if (out.size() > 100000) break;
    }
    const QString trimmed = out.trimmed();
    if (trimmed.isEmpty()) return QString();
    if (trimmed.contains(QStringLiteral("No searchable text"), Qt::CaseInsensitive)) return QString();
    if (trimmed.contains(QStringLiteral("no searchable text"), Qt::CaseInsensitive)) return QString();
    return trimmed;
#elif defined(HAVE_POPPLER_QT6)
    std::unique_ptr<Poppler::Document> pdf(Poppler::Document::load(clean));
    if (!pdf) return QString();
    const int pages = pdf->numPages();
    QString out;
    for (int i = 0; i < pages; ++i) {
        std::unique_ptr<Poppler::Page> page(pdf->page(i));
        if (!page) continue;
        const QString pageText = page->text(QRectF());
        if (!pageText.trimmed().isEmpty()) {
            out += pageText;
            out += QStringLiteral("\n");
        }
        if (out.size() > 100000) break;
    }
    const QString trimmed = out.trimmed();
    if (trimmed.isEmpty()) return QString();
    if (trimmed.contains(QStringLiteral("No searchable text"), Qt::CaseInsensitive)) return QString();
    if (trimmed.contains(QStringLiteral("no searchable text"), Qt::CaseInsensitive)) return QString();
    return trimmed;
#else
    Q_UNUSED(clean);
    return QString();
#endif
}

#include "DocumentParser.h"
#include "miniz.h"
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QThread>
#include <QXmlStreamReader>
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

// Forward decl: helpers defined below
static std::string extractZipEntryQ(const QString &zipPath, const std::string &entryName);
static std::string extractZipEntry(const std::string &zipPath, const std::string &entryName);

/// Strip XML/HTML-like tags and collapse whitespace; removes stray '<' '>' for AI-safe plain text.
static QString cleanseXmlTagNoise(const QString &raw)
{
    QString s = raw;
    static const QRegularExpression tagRe(QStringLiteral("<[^>]*>"));
    s.replace(tagRe, QStringLiteral(" "));
    static const QRegularExpression wsRe(QStringLiteral("\\s+"));
    s.replace(wsRe, QStringLiteral(" "));
    s = s.trimmed();
    s.remove(QLatin1Char('<'));
    s.remove(QLatin1Char('>'));
    return s.trimmed();
}

static QString extractOdfViaCleansedContentXml(const QString &abs)
{
    try {
        const std::string xmlContent = extractZipEntryQ(abs, "content.xml");
        if (xmlContent.rfind("DEBUG:", 0) == 0 || xmlContent.empty())
            return QString();
        return cleanseXmlTagNoise(QString::fromStdString(xmlContent)).trimmed();
    } catch (...) {
        return QString();
    }
}

/// EPUB (ZIP): concatenate .html/.htm/.xhtml bodies, then tag-stripped plain text.
static QString extractEpubPlainCleansed(const QString &abs)
{
    try {
        QFile file(abs);
        if (!file.open(QIODevice::ReadOnly)) {
            qDebug() << "EPUB Error: cannot open" << abs;
            return QString();
        }
        const QByteArray fileData = file.readAll();
        file.close();
        if (fileData.isEmpty()) return QString();

        mz_zip_archive zip_archive;
        memset(&zip_archive, 0, sizeof(zip_archive));
        if (!mz_zip_reader_init_mem(&zip_archive, fileData.constData(), static_cast<mz_uint>(fileData.size()), 0)) {
            qDebug() << "EPUB Error: zip init failed" << abs;
            return QString();
        }

        QStringList entryPaths;
        const int num_files = mz_zip_reader_get_num_files(&zip_archive);
        for (int i = 0; i < num_files; ++i) {
            mz_zip_archive_file_stat st{};
            if (!mz_zip_reader_file_stat(&zip_archive, i, &st)) continue;
            const QString name = QString::fromUtf8(st.m_filename);
            const QString ln = name.toLower();
            if (!(ln.endsWith(QStringLiteral(".html")) || ln.endsWith(QStringLiteral(".htm"))
                  || ln.endsWith(QStringLiteral(".xhtml"))))
                continue;
            entryPaths << name;
        }
        std::sort(entryPaths.begin(), entryPaths.end(), [](const QString &a, const QString &b) {
            return a.localeAwareCompare(b) < 0;
        });

        QString blob;
        for (const QString &entryName : entryPaths) {
            const QByteArray enc = entryName.toUtf8();
            const int zi = mz_zip_reader_locate_file(&zip_archive, enc.constData(), nullptr, 0);
            if (zi < 0) continue;
            size_t file_size = 0;
            void *pHeap = mz_zip_reader_extract_to_heap(&zip_archive, zi, &file_size, 0);
            if (!pHeap || file_size == 0) {
                if (pHeap) mz_free(pHeap);
                continue;
            }
            blob += QString::fromUtf8(static_cast<const char *>(pHeap), static_cast<int>(file_size));
            blob += QLatin1Char('\n');
            mz_free(pHeap);
            if (blob.size() > 500000) break;
        }
        mz_zip_reader_end(&zip_archive);
        if (blob.trimmed().isEmpty()) {
            qDebug() << "EPUB Error: no html/xhtml in archive" << abs;
            return QString();
        }
        return cleanseXmlTagNoise(blob).trimmed();
    } catch (const std::exception &e) {
        qDebug() << "EPUB Error: exception" << e.what() << abs;
        return QString();
    } catch (...) {
        qDebug() << "EPUB Error: unknown exception" << abs;
        return QString();
    }
}

std::string DocumentParser::extractText(const std::string& filePath)
{
    return extractTextQString(QString::fromStdString(filePath)).toStdString();
}

QString DocumentParser::extractTextQString(const QString& filePath)
{
    const QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) return QString();

    const QString suffix = fi.suffix().toLower();
    const QString abs = fi.absoluteFilePath();

    // Legacy binary formats: never try to unzip/parse.
    if (suffix == QStringLiteral("doc") || suffix == QStringLiteral("xls") || suffix == QStringLiteral("ppt")) {
        return QString();
    }

    if (suffix == QStringLiteral("docx") || suffix == QStringLiteral("docm") || suffix == QStringLiteral("dotx")
        || suffix == QStringLiteral("dotm")) {
        const QByteArray utf8 = abs.toUtf8();
        const QString s = QString::fromStdString(
            parsDocx(std::string(utf8.constData(), static_cast<size_t>(utf8.size()))));
        return cleanseXmlTagNoise(s).trimmed();
    }
    if (suffix == QStringLiteral("xlsx") || suffix == QStringLiteral("xlsm") || suffix == QStringLiteral("xltx")
        || suffix == QStringLiteral("xltm")) {
        const QByteArray utf8 = abs.toUtf8();
        const QString s = QString::fromStdString(
            parseXlsx(std::string(utf8.constData(), static_cast<size_t>(utf8.size()))));
        if (s.startsWith(QLatin1Char('(')) && s.contains(QStringLiteral("XLSX"))) return s;
        return cleanseXmlTagNoise(s).trimmed();
    }
    if (suffix == QStringLiteral("pptx") || suffix == QStringLiteral("pptm") || suffix == QStringLiteral("potx")
        || suffix == QStringLiteral("potm")) {
        try {
            QFile file(abs);
            if (!file.open(QIODevice::ReadOnly)) {
                qDebug() << "PPTX Error: cannot open file" << abs;
                return QString();
            }
            const QByteArray fileData = file.readAll();
            file.close();
            if (fileData.isEmpty()) {
                qDebug() << "PPTX Error: empty file" << abs;
                return QString();
            }

            mz_zip_archive zip_archive;
            memset(&zip_archive, 0, sizeof(zip_archive));
            if (!mz_zip_reader_init_mem(&zip_archive, fileData.constData(), static_cast<mz_uint>(fileData.size()), 0)) {
                qDebug() << "PPTX Error: mz_zip_reader_init_mem failed" << abs << "size" << fileData.size();
                return QString();
            }

            QString text;
            static const QRegularExpression atRe(
                QStringLiteral("<[a-zA-Z0-9]+:t[^>]*>(.*?)</[a-zA-Z0-9]+:t>"),
                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
            static const QRegularExpression legacyAtRe(
                QStringLiteral("<a:t[^>]*>([\\s\\S]*?)</a:t>"),
                QRegularExpression::CaseInsensitiveOption);

            auto appendMatches = [&](const QString &xmlContent) {
                const QRegularExpression *patterns[] = {&atRe, &legacyAtRe};
                for (const QRegularExpression *re : patterns) {
                    auto git = re->globalMatch(xmlContent);
                    while (git.hasNext()) {
                        const auto m = git.next();
                        if (!m.hasMatch()) continue;
                        const QString chunk = m.captured(1).trimmed();
                        if (!chunk.isEmpty()) {
                            text += chunk;
                            text += QLatin1Char(' ');
                        }
                        if (text.size() > 8000) return;
                    }
                }
            };

            bool anyXml = false;
            QString strippedSlides;
            const int num_files = mz_zip_reader_get_num_files(&zip_archive);
            for (int i = 0; i < num_files; ++i) {
                mz_zip_archive_file_stat st{};
                if (!mz_zip_reader_file_stat(&zip_archive, i, &st)) continue;
                const QString name = QString::fromUtf8(st.m_filename);
                const bool inSlides = name.contains(QStringLiteral("/slides/"), Qt::CaseInsensitive)
                                      && name.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)
                                      && !name.contains(QStringLiteral("/_rels/"), Qt::CaseInsensitive);
                const bool inNotes = name.contains(QStringLiteral("/notesSlides/"), Qt::CaseInsensitive)
                                     && name.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)
                                     && !name.contains(QStringLiteral("/_rels/"), Qt::CaseInsensitive);
                if (!inSlides && !inNotes) continue;

                anyXml = true;
                size_t file_size = 0;
                void *pHeap = mz_zip_reader_extract_to_heap(&zip_archive, i, &file_size, 0);
                if (!pHeap || file_size == 0) {
                    if (pHeap) mz_free(pHeap);
                    continue;
                }
                const QString xmlContent =
                    QString::fromUtf8(static_cast<const char *>(pHeap), static_cast<int>(file_size));
                mz_free(pHeap);

                appendMatches(xmlContent);
                {
                    const QString slidePlain = cleanseXmlTagNoise(xmlContent);
                    if (!slidePlain.isEmpty()) {
                        strippedSlides += slidePlain;
                        strippedSlides += QLatin1Char(' ');
                    }
                    if (strippedSlides.size() > 12000)
                        strippedSlides = strippedSlides.left(12000);
                }
                if (text.size() > 8000) break;
            }

            mz_zip_reader_end(&zip_archive);

            if (!anyXml) {
                qDebug() << "PPTX Error: no slide/notesSlide xml found in archive" << abs;
            } else if (text.trimmed().isEmpty()) {
                qDebug() << "PPTX Error: regex found no text in matched slides" << abs;
            }
            const QString primary = text.trimmed().isEmpty() ? strippedSlides.trimmed() : text;
            return cleanseXmlTagNoise(primary).trimmed();
        } catch (const std::exception &e) {
            qDebug() << "PPTX Error: exception" << e.what() << filePath;
            return QString();
        } catch (...) {
            qDebug() << "PPTX Error: unknown exception" << filePath;
            return QString();
        }
    }
    if (suffix == QStringLiteral("odt") || suffix == QStringLiteral("ods") || suffix == QStringLiteral("odp")) {
        return extractOdfViaCleansedContentXml(abs);
    }
    if (suffix == QStringLiteral("epub")) {
        return extractEpubPlainCleansed(abs);
    }
    if (suffix == QStringLiteral("pdf")) {
        return extractPdfText(abs);
    }
    return QString();
}



// Helper to unzip specific file from archive to string
static std::string extractZipEntryQ(const QString& zipPath, const std::string& entryName) {
    QFile file(zipPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return "DEBUG: Failed to open file via Qt (Unicode check): " + zipPath.toStdString();
    }
    QByteArray fileData = file.readAll();
    file.close();

    if (fileData.isEmpty()) {
        return "DEBUG: File is empty: " + zipPath.toStdString();
    }

    mz_zip_archive zip_archive;
    memset(&zip_archive, 0, sizeof(zip_archive));

    if (!mz_zip_reader_init_mem(&zip_archive, fileData.constData(), static_cast<mz_uint>(fileData.size()), 0)) {
        return "DEBUG: Failed to parse zip structure from memory: " + zipPath.toStdString();
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

static std::string extractZipEntry(const std::string& zipPath, const std::string& entryName) {
    return extractZipEntryQ(QString::fromStdString(zipPath), entryName);
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
        qDebug() << "PDF Error: load failed" << clean << static_cast<int>(err);
        return QString();
    }

    // load() can return before the document is Ready; wait briefly so text APIs work.
    QElapsedTimer waitClock;
    waitClock.start();
    while (doc.status() == QPdfDocument::Status::Loading && waitClock.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QThread::msleep(2);
    }
    if (doc.status() != QPdfDocument::Status::Ready || doc.pageCount() <= 0) {
        qDebug() << "PDF Error: document not ready or empty page count" << clean
                 << "status" << static_cast<int>(doc.status()) << "pages" << doc.pageCount();
        return QString();
    }

    const int pages = doc.pageCount();
    QString out;
    for (int i = 0; i < pages; ++i) {
        QSizeF sz = doc.pagePointSize(i);
        if (sz.width() <= 1 || sz.height() <= 1) {
            qDebug() << "PDF Text Empty: invalid page size" << clean << "page" << i << sz;
            continue;
        }

        QString pageText;
        {
            const QPdfSelection allSel = doc.getAllText(i);
            pageText = allSel.text();
            qDebug() << "Page" << i << "Text:" << pageText.left(50) << "len" << pageText.size()
                     << "getAllText valid" << allSel.isValid();
        }

        if (pageText.trimmed().isEmpty()) {
            const int maxPass = 3;
            for (int pass = 0; pass < maxPass && pageText.trimmed().isEmpty(); ++pass) {
                const qreal w = sz.width() * (1.0 + 0.25 * pass);
                const qreal h = sz.height() * (1.0 + 0.25 * pass);
                const QPdfSelection sel = doc.getSelection(i, QPointF(0, 0), QPointF(w, h));
                pageText = sel.text();
            }
            qDebug() << "Page" << i << "fallback getSelection Text:" << pageText.left(50)
                     << "len" << pageText.size();
        }

        if (pageText.trimmed().isEmpty()) {
            qDebug() << "PDF Text Empty:" << clean << "page" << i << "/" << pages;
        } else {
            out += pageText;
            out += QStringLiteral("\n");
        }
        if (out.size() > 100000) break;
    }
    qDebug() << "PDF extract total chars" << out.size() << "file" << clean;
    return out.trimmed();
#elif defined(HAVE_POPPLER_QT6)
    std::unique_ptr<Poppler::Document> pdf(Poppler::Document::load(clean));
    if (!pdf) {
        qDebug() << "PDF Error: Poppler::Document::load failed" << clean;
        return QString();
    }
    const int pages = pdf->numPages();
    QString out;
    for (int i = 0; i < pages; ++i) {
        std::unique_ptr<Poppler::Page> page(pdf->page(i));
        if (!page) continue;
        QString pageText = page->text(QRectF());
        if (pageText.trimmed().isEmpty()) {
            const QRectF r = page->pageRectF();
            pageText = page->text(r);
        }
        qDebug() << "Page" << i << "Text:" << pageText.left(50) << "len" << pageText.size();
        if (pageText.trimmed().isEmpty()) {
            qDebug() << "PDF Text Empty:" << clean << "page" << i << "/" << pages;
        } else {
            out += pageText;
            out += QStringLiteral("\n");
        }
        if (out.size() > 100000) break;
    }
    qDebug() << "PDF (Poppler) extract total chars" << out.size() << "file" << clean;
    return out.trimmed();
#else
    Q_UNUSED(clean);
    qDebug() << "PDF Error: built without Qt Pdf / Poppler" << filePath;
    return QString();
#endif
}

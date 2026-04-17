#include "DocumentParser.h"
#include "miniz.h"
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

std::string DocumentParser::extractText(const std::string& filePath)
{
    fs::path p(filePath);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".docx") {
        return parsDocx(filePath);
    } else if (ext == ".xlsx") {
        return parseXlsx(filePath);
    } else if (ext == ".pdf") {
        return parsePdf(filePath);
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

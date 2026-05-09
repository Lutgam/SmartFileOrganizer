#ifndef DOCUMENTPARSER_H
#define DOCUMENTPARSER_H

#include <string>
#include <QString>

class DocumentParser
{
public:
    static std::string extractText(const std::string& filePath);
    static QString extractPdfText(const QString& filePath);

private:
    static std::string parsDocx(const std::string& filePath);
    static std::string parseXlsx(const std::string& filePath);
    static std::string parsePdf(const std::string& filePath);
};

#endif // DOCUMENTPARSER_H

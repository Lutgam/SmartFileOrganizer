#ifndef LANGUAGEMANAGER_H
#define LANGUAGEMANAGER_H

#include <QObject>
#include <QString>

class LanguageManager : public QObject {
    Q_OBJECT

public:
    enum class Language { ZH_TW, EN_US };
    Q_ENUM(Language)

    static LanguageManager &instance();

    Language language() const;
    void setLanguage(Language lang);

    QString getText(const QString &key) const;

signals:
    void languageChanged(Language lang);

private:
    explicit LanguageManager(QObject *parent = nullptr);
    Language m_lang = Language::ZH_TW;
};

#endif


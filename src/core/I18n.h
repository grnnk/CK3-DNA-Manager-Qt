#pragma once

#include <QHash>
#include <QLocale>
#include <QObject>
#include <QStringList>

class I18n : public QObject
{
    Q_OBJECT

public:
    explicit I18n(QObject *parent = nullptr);

    bool load(const QString &path, QString *error = nullptr);

    QStringList availableLanguages() const { return m_languageOrder; }
    QString languageName(const QString &code) const;
    QString currentLanguage() const { return m_current; }
    bool hasLanguage(const QString &code) const;

    void setLanguage(const QString &code);

    QString t(const QString &key) const;
    QString t(const QString &key, const QString &argument1) const;
    QString t(const QString &key, const QString &argument1, const QString &argument2) const;

    QLocale locale() const;
    QStringList keysForLanguage(const QString &code) const;

signals:
    void languageChanged(const QString &code);

private:
    QString value(const QString &language, const QString &key) const;

    QHash<QString, QHash<QString, QString>> m_strings;
    QHash<QString, QString> m_languageNames;
    QStringList m_languageOrder;
    QString m_current = QStringLiteral("en");
};

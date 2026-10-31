#include "core/I18n.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

I18n::I18n(QObject *parent)
    : QObject(parent)
{
}

bool I18n::load(const QString &path, QString *error)
{
    m_strings.clear();
    m_languageNames.clear();
    m_languageOrder.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open translations: %1").arg(path);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject()) {
        if (error) {
            *error = parseError.error == QJsonParseError::NoError
                ? QStringLiteral("Translations are not a JSON object")
                : QStringLiteral("Translation parse error at offset %1: %2")
                      .arg(parseError.offset)
                      .arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonArray languages = root.value(QStringLiteral("languages")).toArray();
    for (const QJsonValue &value : languages) {
        const QJsonObject object = value.toObject();
        const QString code = object.value(QStringLiteral("code")).toString();
        const QString name = object.value(QStringLiteral("nativeName")).toString(
            object.value(QStringLiteral("name")).toString());
        if (code.isEmpty())
            continue;
        m_languageOrder.append(code);
        m_languageNames.insert(code, name.isEmpty() ? code : name);
    }

    const QJsonObject strings = root.value(QStringLiteral("strings")).toObject();
    for (auto languageIt = strings.begin(); languageIt != strings.end(); ++languageIt) {
        if (!languageIt.value().isObject())
            continue;
        QHash<QString, QString> values;
        const QJsonObject object = languageIt.value().toObject();
        for (auto stringIt = object.begin(); stringIt != object.end(); ++stringIt) {
            if (stringIt.value().isString())
                values.insert(stringIt.key(), stringIt.value().toString());
        }
        m_strings.insert(languageIt.key(), values);
    }

    if (!m_strings.contains(QStringLiteral("en"))) {
        if (error)
            *error = QStringLiteral("Translations do not contain English strings");
        return false;
    }

    if (!m_languageOrder.contains(QStringLiteral("en")))
        m_languageOrder.prepend(QStringLiteral("en"));
    if (!m_languageNames.contains(QStringLiteral("en")))
        m_languageNames.insert(QStringLiteral("en"), QStringLiteral("English"));

    if (!m_strings.contains(m_current))
        m_current = QStringLiteral("en");

    if (error)
        error->clear();
    return true;
}

bool I18n::hasLanguage(const QString &code) const
{
    return m_strings.contains(code);
}

QString I18n::languageName(const QString &code) const
{
    return m_languageNames.value(code, code);
}

void I18n::setLanguage(const QString &code)
{
    if (code == m_current || !m_strings.contains(code))
        return;
    m_current = code;
    emit languageChanged(m_current);
}

QString I18n::value(const QString &language, const QString &key) const
{
    const auto languageIt = m_strings.constFind(language);
    if (languageIt != m_strings.cend()) {
        const auto valueIt = languageIt->constFind(key);
        if (valueIt != languageIt->cend())
            return valueIt.value();
    }
    return {};
}

QString I18n::t(const QString &key) const
{
    QString text = value(m_current, key);
    if (text.isEmpty())
        text = value(QStringLiteral("en"), key);
    return text.isEmpty() ? key : text;
}

QString I18n::t(const QString &key, const QString &argument1) const
{
    return t(key).arg(argument1);
}

QString I18n::t(const QString &key, const QString &argument1, const QString &argument2) const
{
    return t(key).arg(argument1).arg(argument2);
}

QLocale I18n::locale() const
{
    return QLocale(m_current);
}

QStringList I18n::keysForLanguage(const QString &code) const
{
    QStringList keys;
    const auto it = m_strings.constFind(code);
    if (it == m_strings.cend())
        return keys;
    keys.reserve(it->size());
    for (auto keyIt = it->cbegin(); keyIt != it->cend(); ++keyIt)
        keys.append(keyIt.key());
    keys.sort();
    return keys;
}

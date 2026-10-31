#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

struct GeneOption
{
    QString id;
    int index = 0;
    QString label;
};

struct GeneDefinition
{
    QString key;
    QString name;
    QString category;
    QString kind; // "color" or "enum"
    bool inBase64 = false;
    int base64Index = -1;
    QVector<GeneOption> options;

    bool isColor() const { return kind == QStringLiteral("color"); }
};

/// Data-driven CK3 gene catalog loaded from resources/genes.json.
class GeneCatalog
{
public:
    bool load(const QString &path, QString *error = nullptr);
    bool isLoaded() const { return !m_genes.isEmpty(); }

    const QVector<GeneDefinition> &genes() const { return m_genes; }
    const GeneDefinition *byKey(const QString &key) const;
    const GeneDefinition *byBase64Index(int index) const;
    int indexOfKey(const QString &key) const;

    int base64GeneCount() const { return m_base64Order.size(); }
    const QVector<int> &base64Order() const { return m_base64Order; }

    const QStringList &categoryOrder() const { return m_categoryOrder; }
    QStringList categoriesInUse() const;

    const GeneOption *optionByIndex(const QString &key, int index) const;
    int optionIndexById(const QString &key, const QString &id) const;
    QString optionLabel(const QString &key, int index) const;
    QString optionId(const QString &key, int index) const;

    QString displayName(const QString &key) const;

    static QString categoryTranslationKey(const QString &category);

private:
    QVector<GeneDefinition> m_genes;
    QVector<int> m_base64Order;
    QHash<QString, int> m_indexByKey;
    QStringList m_categoryOrder;
};

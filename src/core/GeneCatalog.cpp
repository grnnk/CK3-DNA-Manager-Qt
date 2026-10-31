#include "core/GeneCatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>

namespace {

QString readError(const QJsonParseError &error)
{
    return QStringLiteral("JSON parse error at offset %1: %2")
        .arg(error.offset)
        .arg(error.errorString());
}

} // namespace

bool GeneCatalog::load(const QString &path, QString *error)
{
    m_genes.clear();
    m_base64Order.clear();
    m_indexByKey.clear();
    m_categoryOrder.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open gene catalog: %1").arg(path);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject()) {
        if (error)
            *error = parseError.error == QJsonParseError::NoError
                ? QStringLiteral("Gene catalog is not a JSON object")
                : readError(parseError);
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonArray categoryArray = root.value(QStringLiteral("categoryOrder")).toArray();
    for (const QJsonValue &value : categoryArray) {
        const QString category = value.toString();
        if (!category.isEmpty() && !m_categoryOrder.contains(category))
            m_categoryOrder.append(category);
    }

    const QJsonArray genes = root.value(QStringLiteral("genes")).toArray();
    m_genes.reserve(genes.size());
    QSet<QString> seenKeys;
    QSet<int> seenBase64;

    for (const QJsonValue &value : genes) {
        if (!value.isObject()) {
            if (error)
                *error = QStringLiteral("Gene catalog contains a non-object gene entry");
            return false;
        }
        const QJsonObject object = value.toObject();
        GeneDefinition gene;
        gene.key = object.value(QStringLiteral("key")).toString();
        gene.name = object.value(QStringLiteral("name")).toString();
        gene.category = object.value(QStringLiteral("category")).toString();
        gene.kind = object.value(QStringLiteral("kind")).toString();
        gene.inBase64 = object.value(QStringLiteral("inBase64")).toBool();
        gene.base64Index = object.value(QStringLiteral("base64Index")).toInt(-1);

        if (gene.key.isEmpty()) {
            if (error)
                *error = QStringLiteral("Gene catalog contains an entry without a key");
            return false;
        }
        if (gene.name.isEmpty())
            gene.name = gene.key;
        if (seenKeys.contains(gene.key)) {
            if (error)
                *error = QStringLiteral("Duplicate gene key in catalog: %1").arg(gene.key);
            return false;
        }
        seenKeys.insert(gene.key);

        const QJsonArray optionArray = object.value(QStringLiteral("options")).toArray();
        gene.options.reserve(optionArray.size());
        for (const QJsonValue &optionValue : optionArray) {
            const QJsonObject optionObject = optionValue.toObject();
            GeneOption option;
            option.id = optionObject.value(QStringLiteral("id")).toString();
            option.index = optionObject.value(QStringLiteral("index")).toInt();
            option.label = optionObject.value(QStringLiteral("label")).toString();
            if (option.id.isEmpty())
                continue;
            if (option.label.isEmpty())
                option.label = option.id;
            gene.options.append(option);
        }
        std::sort(gene.options.begin(), gene.options.end(),
                  [](const GeneOption &a, const GeneOption &b) { return a.index < b.index; });

        if (gene.inBase64) {
            if (gene.base64Index < 0 || seenBase64.contains(gene.base64Index)) {
                if (error)
                    *error = QStringLiteral("Invalid or duplicate Base64 index for %1").arg(gene.key);
                return false;
            }
            seenBase64.insert(gene.base64Index);
            m_base64Order.append(m_genes.size());
        }

        m_indexByKey.insert(gene.key, m_genes.size());
        m_genes.append(std::move(gene));
    }

    if (m_genes.isEmpty()) {
        if (error)
            *error = QStringLiteral("Gene catalog is empty");
        return false;
    }

    std::sort(m_base64Order.begin(), m_base64Order.end(), [this](int a, int b) {
        return m_genes.at(a).base64Index < m_genes.at(b).base64Index;
    });
    for (int i = 0; i < m_base64Order.size(); ++i) {
        if (m_genes.at(m_base64Order.at(i)).base64Index != i) {
            if (error) {
                *error = QStringLiteral("Base64 gene indexes are not contiguous (slot %1)").arg(i);
            }
            return false;
        }
    }

    if (error)
        error->clear();
    return true;
}

const GeneDefinition *GeneCatalog::byKey(const QString &key) const
{
    const auto it = m_indexByKey.constFind(key);
    if (it == m_indexByKey.cend())
        return nullptr;
    return &m_genes.at(it.value());
}

const GeneDefinition *GeneCatalog::byBase64Index(int index) const
{
    if (index < 0 || index >= m_base64Order.size())
        return nullptr;
    return &m_genes.at(m_base64Order.at(index));
}

int GeneCatalog::indexOfKey(const QString &key) const
{
    return m_indexByKey.value(key, -1);
}

QStringList GeneCatalog::categoriesInUse() const
{
    QStringList result;
    for (const QString &category : m_categoryOrder) {
        const bool used = std::any_of(m_genes.cbegin(), m_genes.cend(),
                                      [&category](const GeneDefinition &gene) {
                                          return gene.category == category;
                                      });
        if (used)
            result.append(category);
    }
    // Include any category that appears in the catalog but not in categoryOrder.
    for (const GeneDefinition &gene : m_genes) {
        if (!gene.category.isEmpty() && !result.contains(gene.category))
            result.append(gene.category);
    }
    return result;
}

const GeneOption *GeneCatalog::optionByIndex(const QString &key, int index) const
{
    const GeneDefinition *gene = byKey(key);
    if (!gene)
        return nullptr;
    for (const GeneOption &option : gene->options) {
        if (option.index == index)
            return &option;
    }
    return nullptr;
}

int GeneCatalog::optionIndexById(const QString &key, const QString &id) const
{
    const GeneDefinition *gene = byKey(key);
    if (!gene)
        return -1;
    for (const GeneOption &option : gene->options) {
        if (option.id == id)
            return option.index;
    }
    return -1;
}

QString GeneCatalog::optionLabel(const QString &key, int index) const
{
    const GeneOption *option = optionByIndex(key, index);
    return option ? option->label
                  : QStringLiteral("Unknown (%1)").arg(index);
}

QString GeneCatalog::optionId(const QString &key, int index) const
{
    const GeneOption *option = optionByIndex(key, index);
    return option ? option->id : QString();
}

QString GeneCatalog::displayName(const QString &key) const
{
    const GeneDefinition *gene = byKey(key);
    return gene ? gene->name : key;
}

QString GeneCatalog::categoryTranslationKey(const QString &category)
{
    return QStringLiteral("genes.category.") + category;
}

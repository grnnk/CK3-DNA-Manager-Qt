#include "core/DnaCodec.h"

#include "core/GeneCatalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSet>

#include <algorithm>
#include <cctype>

namespace {

constexpr int kBytesPerGene = 4;
constexpr int kVanillaGeneCount = 104;

std::array<quint8, 4> readFour(const QByteArray &bytes, int geneIndex)
{
    const int offset = geneIndex * kBytesPerGene;
    return {
        static_cast<quint8>(bytes.at(offset)),
        static_cast<quint8>(bytes.at(offset + 1)),
        static_cast<quint8>(bytes.at(offset + 2)),
        static_cast<quint8>(bytes.at(offset + 3)),
    };
}

GenePair pairFromBytes(const std::array<quint8, 4> &bytes)
{
    GenePair pair;
    pair.first.variant = bytes[0];
    pair.first.intensity = bytes[1];
    pair.second.variant = bytes[2];
    pair.second.intensity = bytes[3];
    return pair;
}

QByteArray normalizeBase64(const QString &text, QString *error)
{
    QString compact;
    compact.reserve(text.size());
    for (const QChar character : text) {
        if (character.isSpace())
            continue;
        if (character == QLatin1Char('-'))
            compact.append(QLatin1Char('+'));
        else if (character == QLatin1Char('_'))
            compact.append(QLatin1Char('/'));
        else
            compact.append(character);
    }

    if (compact.isEmpty()) {
        if (error)
            *error = QStringLiteral("The DNA code is empty.");
        return {};
    }

    QByteArray input = compact.toLatin1();
    for (const char character : input) {
        const bool valid = (character >= 'A' && character <= 'Z')
            || (character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9')
            || character == '+' || character == '/' || character == '=';
        if (!valid) {
            if (error) {
                *error = QStringLiteral("The DNA code contains an invalid Base64 character.");
            }
            return {};
        }
    }

    while (input.size() % 4 != 0)
        input.append('=');
    return input;
}

bool hasGenesBlock(const QString &text)
{
    static const QRegularExpression expression(
        QStringLiteral("\\bgenes\\s*=\\s*\\{"));
    return expression.match(text).hasMatch();
}

struct RulerToken
{
    enum Kind { Word, String, Equal, LBrace, RBrace };
    Kind kind = Word;
    QString text;
    int start = 0;
    int end = 0;
};

QVector<RulerToken> tokenizeRuler(const QString &text, QString *error)
{
    QVector<RulerToken> tokens;
    int i = 0;
    while (i < text.size()) {
        const QChar character = text.at(i);
        if (character.isSpace()) {
            ++i;
            continue;
        }
        if (character == QLatin1Char('#')) {
            while (i < text.size() && text.at(i) != QLatin1Char('\n'))
                ++i;
            continue;
        }
        if (character == QLatin1Char('=')) {
            tokens.append({ RulerToken::Equal, QStringLiteral("="), i, i + 1 });
            ++i;
            continue;
        }
        if (character == QLatin1Char('{')) {
            tokens.append({ RulerToken::LBrace, QStringLiteral("{"), i, i + 1 });
            ++i;
            continue;
        }
        if (character == QLatin1Char('}')) {
            tokens.append({ RulerToken::RBrace, QStringLiteral("}"), i, i + 1 });
            ++i;
            continue;
        }
        if (character == QLatin1Char('"')) {
            const int tokenStart = i;
            ++i;
            QString value;
            bool closed = false;
            while (i < text.size()) {
                const QChar current = text.at(i);
                if (current == QLatin1Char('"')) {
                    closed = true;
                    ++i;
                    break;
                }
                if (current == QLatin1Char('\\') && i + 1 < text.size()) {
                    value.append(text.at(i + 1));
                    i += 2;
                    continue;
                }
                value.append(current);
                ++i;
            }
            if (!closed) {
                if (error)
                    *error = QStringLiteral("Unterminated string in Ruler Designer code.");
                return {};
            }
            tokens.append({ RulerToken::String, value, tokenStart, i });
            continue;
        }

        const int start = i;
        while (i < text.size()) {
            const QChar current = text.at(i);
            if (current.isSpace() || current == QLatin1Char('{')
                || current == QLatin1Char('}') || current == QLatin1Char('=')
                || current == QLatin1Char('"') || current == QLatin1Char('#')) {
                break;
            }
            ++i;
        }
        if (i == start) {
            ++i;
            continue;
        }
        tokens.append({ RulerToken::Word, text.mid(start, i - start), start, i });
    }
    return tokens;
}

bool numericWord(const RulerToken &token, int *value = nullptr)
{
    bool ok = false;
    const int number = token.kind == RulerToken::Word ? token.text.toInt(&ok) : 0;
    if (ok && value)
        *value = number;
    return ok;
}

int byteValue(const RulerToken &token, QStringList *warnings, const QString &geneKey)
{
    bool ok = false;
    const int value = token.kind == RulerToken::Word ? token.text.toInt(&ok) : 0;
    if (!ok) {
        if (warnings) {
            warnings->append(QStringLiteral("Expected a number for %1, using 0.")
                                 .arg(geneKey));
        }
        return 0;
    }
    return std::clamp(value, 0, 255);
}

int variantValue(const RulerToken &token,
                 const GeneCatalog &catalog,
                 const QString &geneKey,
                 QStringList *warnings)
{
    if (token.kind == RulerToken::Word) {
        bool ok = false;
        const int value = token.text.toInt(&ok);
        if (ok)
            return std::clamp(value, 0, 255);
    }

    const int index = catalog.optionIndexById(geneKey, token.text);
    if (index >= 0)
        return index;

    if (warnings) {
        warnings->append(QStringLiteral("Unknown option \"%1\" for %2, using 0.")
                             .arg(token.text, geneKey));
    }
    return 0;
}

QString optionToken(const GeneCatalog &catalog, const QString &key, int index)
{
    const QString id = catalog.optionId(key, index);
    if (!id.isEmpty())
        return QStringLiteral("\"%1\"").arg(id);
    return QString::number(index);
}

QString extraKeyForSlot(int geneSlot)
{
    return QStringLiteral("extra_%1").arg(geneSlot + 1);
}

/// Vanilla Ruler Designer fields that are not part of the 104-gene Base64
/// payload. They are kept in the Ruler metadata instead of being appended.
bool isRulerOnlyKey(const QString &key, const GeneCatalog &catalog)
{
    const GeneDefinition *gene = catalog.byKey(key);
    if (gene && !gene->inBase64)
        return true;

    static const QSet<QString> knownRulerOnly = {
        QStringLiteral("clothes"),
        QStringLiteral("clothing"),
        QStringLiteral("custom_beards"),
        QStringLiteral("custom_hair"),
    };
    return knownRulerOnly.contains(key);
}

QString quotedRulerString(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString extraValueToken(const ExtraGene &extra, bool first)
{
    const QString &option = first ? extra.firstOption : extra.secondOption;
    if (!option.isEmpty())
        return quotedRulerString(option);
    return QString::number(static_cast<int>(first ? extra.bytes[0] : extra.bytes[2]));
}

QString extraStrength(const ExtraGene &extra, bool first)
{
    return QString::number(static_cast<int>(first ? extra.bytes[1] : extra.bytes[3]));
}

} // namespace

bool DnaCodec::looksLikeLegacy(const QByteArray &bytes, const GeneCatalog &catalog)
{
    if (bytes.size() != kVanillaGeneCount * kBytesPerGene)
        return false;

    // The only ambiguous tail is slots 99/100: modern hair type + baldness,
    // legacy hairstyle group + beard group. Modern values never exceed 4 / 1.
    const int modernHairType = static_cast<quint8>(bytes.at(99 * kBytesPerGene));
    const int modernBaldness = static_cast<quint8>(bytes.at(100 * kBytesPerGene));
    const GeneDefinition *hairType = catalog.byBase64Index(99);
    const GeneDefinition *baldness = catalog.byBase64Index(100);
    const int hairMax = hairType ? hairType->options.size() - 1 : 4;
    const int baldnessMax = baldness ? baldness->options.size() - 1 : 1;
    return modernHairType > std::max(hairMax, 4) || modernBaldness > std::max(baldnessMax, 1);
}

DnaParseResult DnaCodec::parse(const QString &text,
                               Base64Hint hint,
                               const GeneCatalog &catalog,
                               const GeneCatalog &legacyCatalog)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        DnaParseResult result;
        result.error = QStringLiteral("The DNA code is empty.");
        return result;
    }

    if (trimmed.contains(QStringLiteral("ruler_designer")) || hasGenesBlock(trimmed))
        return parseRuler(trimmed, catalog, legacyCatalog);

    QString error;
    const QByteArray input = normalizeBase64(trimmed, &error);
    if (input.isEmpty()) {
        DnaParseResult result;
        result.error = error;
        return result;
    }

    const QByteArray bytes = QByteArray::fromBase64(
        input, QByteArray::AbortOnBase64DecodingErrors);
    if (bytes.isEmpty()) {
        DnaParseResult result;
        result.error = QStringLiteral("The DNA code could not be decoded as Base64.");
        return result;
    }
    return parseBase64Bytes(bytes, hint, catalog, legacyCatalog);
}

DnaParseResult DnaCodec::fromBytes(const QByteArray &bytes,
                                   Base64Hint hint,
                                   const GeneCatalog &catalog,
                                   const GeneCatalog &legacyCatalog)
{
    return parseBase64Bytes(bytes, hint, catalog, legacyCatalog);
}

DnaParseResult DnaCodec::parseBase64Bytes(const QByteArray &bytes,
                                          Base64Hint hint,
                                          const GeneCatalog &catalog,
                                          const GeneCatalog &legacyCatalog)
{
    DnaParseResult result;
    if (bytes.isEmpty()) {
        result.error = QStringLiteral("The DNA payload is empty.");
        return result;
    }
    if (bytes.size() % kBytesPerGene != 0) {
        result.error = QStringLiteral("The DNA payload has an invalid length (%1 bytes).")
                           .arg(bytes.size());
        return result;
    }

    const int geneCount = bytes.size() / kBytesPerGene;
    if (geneCount < kVanillaGeneCount) {
        result.error = QStringLiteral("The DNA payload contains %1 genes; at least %2 are required.")
                           .arg(geneCount)
                           .arg(kVanillaGeneCount);
        return result;
    }

    bool useLegacy = hint == Base64Hint::Legacy
        || (hint == Base64Hint::Auto && looksLikeLegacy(bytes, catalog));
    if (useLegacy && geneCount > kVanillaGeneCount) {
        useLegacy = false;
        result.warnings.append(QStringLiteral(
            "Legacy format only contains 104 genes; the extra genes were kept as modern data."));
    }

    DnaData raw;
    for (int index = 0; index < kVanillaGeneCount; ++index) {
        const GeneDefinition *gene = catalog.byBase64Index(index);
        if (!gene) {
            result.error = QStringLiteral("The gene catalog is missing Base64 slot %1.").arg(index);
            return result;
        }
        const std::array<quint8, 4> values = readFour(bytes, index);
        if (gene->isColor())
            raw.setColor(gene->key, values);
        else
            raw.setGene(gene->key, pairFromBytes(values));
    }

    for (int index = kVanillaGeneCount; index < geneCount; ++index) {
        ExtraGene extra;
        extra.key = extraKeyForSlot(index);
        extra.bytes = readFour(bytes, index);
        raw.extras.append(extra);
    }

    if (useLegacy) {
        QStringList warnings;
        result.data = convertLegacy(raw, catalog, legacyCatalog, &warnings);
        result.warnings.append(warnings);
        result.format = DnaFormat::LegacyBase64;
        result.legacyConverted = true;
    } else {
        result.data = std::move(raw);
        result.format = DnaFormat::ModernBase64;
    }

    result.geneCount = geneCount;
    result.ok = true;
    return result;
}

DnaData DnaCodec::convertLegacy(const DnaData &legacy,
                                const GeneCatalog &catalog,
                                const GeneCatalog &legacyCatalog,
                                QStringList *warnings)
{
    DnaData converted;
    converted.extras = legacy.extras;

    for (int index = 0; index < catalog.base64GeneCount(); ++index) {
        const GeneDefinition *gene = catalog.byBase64Index(index);
        if (!gene)
            continue;

        if (gene->isColor()) {
            converted.setColor(gene->key, legacy.color(gene->key));
            continue;
        }

        const GenePair source = legacy.gene(gene->key);
        const GeneDefinition *legacyGene = legacyCatalog.byKey(gene->key);

        auto convertAllele = [&](const GeneAllele &allele) {
            GeneAllele result;
            result.intensity = allele.intensity;
            if (!legacyGene) {
                result.variant = 0;
                return result;
            }
            const GeneOption *oldOption = legacyCatalog.optionByIndex(gene->key, allele.variant);
            if (!oldOption) {
                result.variant = 0;
                if (warnings) {
                    warnings->append(QStringLiteral("Legacy %1 has an unknown variant %2.")
                                         .arg(gene->key)
                                         .arg(allele.variant));
                }
                return result;
            }
            const int modernIndex = catalog.optionIndexById(gene->key, oldOption->id);
            if (modernIndex < 0) {
                result.variant = 0;
                if (warnings) {
                    warnings->append(QStringLiteral("Legacy option \"%1\" no longer exists for %2.")
                                         .arg(oldOption->id, gene->key));
                }
            } else {
                result.variant = modernIndex;
            }
            return result;
        };

        GenePair convertedPair;
        convertedPair.first = convertAllele(source.first);
        convertedPair.second = convertAllele(source.second);
        converted.setGene(gene->key, convertedPair);
    }

    return converted;
}

QByteArray DnaCodec::toBytes(const DnaData &data, const GeneCatalog &catalog)
{
    QByteArray bytes;
    bytes.reserve((catalog.base64GeneCount() + data.extras.size()) * kBytesPerGene);

    for (int index = 0; index < catalog.base64GeneCount(); ++index) {
        const GeneDefinition *gene = catalog.byBase64Index(index);
        if (!gene) {
            bytes.append(QByteArray(kBytesPerGene, '\0'));
            continue;
        }
        if (gene->isColor()) {
            const std::array<quint8, 4> values = data.color(gene->key);
            for (const quint8 value : values)
                bytes.append(static_cast<char>(value));
        } else {
            const GenePair pair = data.gene(gene->key);
            bytes.append(static_cast<char>(std::clamp(pair.first.variant, 0, 255)));
            bytes.append(static_cast<char>(std::clamp(pair.first.intensity, 0, 255)));
            bytes.append(static_cast<char>(std::clamp(pair.second.variant, 0, 255)));
            bytes.append(static_cast<char>(std::clamp(pair.second.intensity, 0, 255)));
        }
    }

    for (const ExtraGene &extra : data.extras) {
        if (extra.kind != ExtraGeneKind::Modded)
            continue;
        for (const quint8 value : extra.bytes)
            bytes.append(static_cast<char>(value));
    }

    return bytes;
}

QString DnaCodec::toBase64(const DnaData &data, const GeneCatalog &catalog)
{
    return QString::fromLatin1(toBytes(data, catalog).toBase64());
}

QString DnaCodec::toRuler(const DnaData &data, const GeneCatalog &catalog)
{
    const quint64 designerId = 1000000000ULL
        + QRandomGenerator::global()->bounded(9000000000ULL);

    QString result = QStringLiteral("ruler_designer_%1={").arg(designerId);
    const bool hasMetadata = data.ruler.valid;
    if (hasMetadata && !data.ruler.prefix.isEmpty())
        result += data.ruler.prefix;
    else
        result += QStringLiteral("\n\t");
    result += QStringLiteral("genes={\n");

    QSet<QString> emittedBase64;
    QVector<bool> emittedExtras(data.extras.size(), false);

    const auto emitGene = [&](const GeneDefinition &gene) {
        const QStringList raw = data.ruler.rawTokens.value(gene.key);
        if (raw.size() == 4) {
            result += QStringLiteral("\t\t%1={ %2 }\n")
                          .arg(gene.key, raw.join(QLatin1Char(' ')));
            return;
        }

        if (gene.isColor()) {
            const std::array<quint8, 4> values = data.color(gene.key);
            result += QStringLiteral("\t\t%1={ %2 %3 %4 %5 }\n")
                          .arg(gene.key)
                          .arg(static_cast<int>(values[0]))
                          .arg(static_cast<int>(values[1]))
                          .arg(static_cast<int>(values[2]))
                          .arg(static_cast<int>(values[3]));
            return;
        }

        const GenePair pair = data.gene(gene.key);
        result += QStringLiteral("\t\t%1={ %2 %3 %4 %5 }\n")
                      .arg(gene.key)
                      .arg(optionToken(catalog, gene.key, pair.first.variant))
                      .arg(pair.first.intensity)
                      .arg(optionToken(catalog, gene.key, pair.second.variant))
                      .arg(pair.second.intensity);
    };

    const auto emitExtra = [&](const ExtraGene &extra) {
        const QStringList raw = data.ruler.rawTokens.value(extra.key);
        if (raw.size() == 4) {
            result += QStringLiteral("\t\t%1={ %2 }\n")
                          .arg(extra.key, raw.join(QLatin1Char(' ')));
            return;
        }
        result += QStringLiteral("\t\t%1={ %2 %3 %4 %5 }\n")
                      .arg(extra.key)
                      .arg(extraValueToken(extra, true))
                      .arg(extraStrength(extra, true))
                      .arg(extraValueToken(extra, false))
                      .arg(extraStrength(extra, false));
    };

    const auto findExtra = [&](const QString &key) {
        for (int i = 0; i < data.extras.size(); ++i) {
            if (!emittedExtras.at(i) && data.extras.at(i).key == key)
                return i;
        }
        return -1;
    };

    if (hasMetadata && !data.ruler.order.isEmpty()) {
        for (const QString &key : data.ruler.order) {
            if (const GeneDefinition *gene = catalog.byKey(key);
                gene && gene->inBase64) {
                emitGene(*gene);
                emittedBase64.insert(key);
                continue;
            }

            const int extraIndex = findExtra(key);
            if (extraIndex >= 0) {
                emitExtra(data.extras.at(extraIndex));
                emittedExtras[extraIndex] = true;
            }
        }
    }

    for (int index = 0; index < catalog.base64GeneCount(); ++index) {
        const GeneDefinition *gene = catalog.byBase64Index(index);
        if (gene && !emittedBase64.contains(gene->key)) {
            emitGene(*gene);
            emittedBase64.insert(gene->key);
        }
    }

    for (int index = 0; index < data.extras.size(); ++index) {
        if (!emittedExtras.at(index))
            emitExtra(data.extras.at(index));
    }

    result += QStringLiteral("\t}");
    if (hasMetadata && !data.ruler.suffix.isEmpty())
        result += data.ruler.suffix;
    else
        result += QStringLiteral("\n");
    result += QLatin1Char('}');
    return result;
}

QString DnaCodec::serializeRulerMetadata(const DnaData &data)
{
    if (!data.ruler.valid)
        return {};

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("ck3-ruler-meta"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("prefix"), data.ruler.prefix);
    root.insert(QStringLiteral("suffix"), data.ruler.suffix);

    QJsonArray order;
    for (const QString &key : data.ruler.order)
        order.append(key);
    root.insert(QStringLiteral("order"), order);

    QJsonArray extras;
    for (const ExtraGene &extra : data.extras) {
        QJsonObject object;
        object.insert(QStringLiteral("key"), extra.key);
        object.insert(QStringLiteral("kind"),
                      extra.kind == ExtraGeneKind::RulerOnly
                          ? QStringLiteral("ruler-only")
                          : QStringLiteral("modded"));
        QJsonArray bytes;
        for (const quint8 value : extra.bytes)
            bytes.append(static_cast<int>(value));
        object.insert(QStringLiteral("bytes"), bytes);
        if (!extra.firstOption.isEmpty())
            object.insert(QStringLiteral("firstOption"), extra.firstOption);
        if (!extra.secondOption.isEmpty())
            object.insert(QStringLiteral("secondOption"), extra.secondOption);
        extras.append(object);
    }
    root.insert(QStringLiteral("extras"), extras);

    QJsonObject rawTokens;
    for (auto it = data.ruler.rawTokens.cbegin(); it != data.ruler.rawTokens.cend(); ++it) {
        QJsonArray values;
        for (const QString &token : it.value())
            values.append(token);
        rawTokens.insert(it.key(), values);
    }
    root.insert(QStringLiteral("rawTokens"), rawTokens);

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void DnaCodec::applyRulerMetadata(DnaData &data, const QString &json)
{
    if (json.isEmpty())
        return;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("ck3-ruler-meta")
        || root.value(QStringLiteral("version")).toInt() != 1) {
        return;
    }

    RulerMetadata metadata;
    metadata.valid = true;
    metadata.prefix = root.value(QStringLiteral("prefix")).toString();
    metadata.suffix = root.value(QStringLiteral("suffix")).toString();
    for (const QJsonValue &value : root.value(QStringLiteral("order")).toArray()) {
        if (value.isString())
            metadata.order.append(value.toString());
    }

    const QJsonObject rawTokens = root.value(QStringLiteral("rawTokens")).toObject();
    for (auto it = rawTokens.constBegin(); it != rawTokens.constEnd(); ++it) {
        QStringList tokens;
        for (const QJsonValue &value : it.value().toArray()) {
            if (value.isString())
                tokens.append(value.toString());
        }
        if (tokens.size() == 4)
            metadata.rawTokens.insert(it.key(), tokens);
    }

    QVector<ExtraGene> loadedExtras;
    for (const QJsonValue &value : root.value(QStringLiteral("extras")).toArray()) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        ExtraGene extra;
        extra.key = object.value(QStringLiteral("key")).toString();
        extra.kind = object.value(QStringLiteral("kind")).toString()
                             == QStringLiteral("ruler-only")
                         ? ExtraGeneKind::RulerOnly
                         : ExtraGeneKind::Modded;
        const QJsonArray byteValues = object.value(QStringLiteral("bytes")).toArray();
        for (int i = 0; i < 4 && i < byteValues.size(); ++i) {
            extra.bytes[i] = static_cast<quint8>(
                std::clamp(byteValues.at(i).toInt(), 0, 255));
        }
        extra.firstOption = object.value(QStringLiteral("firstOption")).toString();
        extra.secondOption = object.value(QStringLiteral("secondOption")).toString();
        if (!extra.key.isEmpty())
            loadedExtras.append(extra);
    }

    // fromBytes() creates anonymous extra slots for every byte after the 104
    // vanilla genes. Restore their saved names/options in slot order.
    int byteExtraIndex = 0;
    for (const ExtraGene &saved : loadedExtras) {
        if (saved.kind != ExtraGeneKind::Modded)
            continue;
        if (byteExtraIndex < data.extras.size()) {
            ExtraGene &target = data.extras[byteExtraIndex++];
            target.key = saved.key;
            target.kind = ExtraGeneKind::Modded;
            target.firstOption = saved.firstOption;
            target.secondOption = saved.secondOption;
        } else {
            data.extras.append(saved);
        }
    }

    // Ruler-only genes have no bytes in the Base64 payload.
    for (const ExtraGene &saved : loadedExtras) {
        if (saved.kind == ExtraGeneKind::RulerOnly)
            data.extras.append(saved);
    }

    data.ruler = metadata;
}

DnaParseResult DnaCodec::parseRuler(const QString &text,
                                    const GeneCatalog &catalog,
                                    const GeneCatalog &legacyCatalog)
{
    Q_UNUSED(legacyCatalog)

    DnaParseResult result;
    QString tokenError;
    const QVector<RulerToken> tokens = tokenizeRuler(text, &tokenError);
    if (!tokenError.isEmpty()) {
        result.error = tokenError;
        return result;
    }

    int genesToken = -1;
    for (int i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens.at(i).kind == RulerToken::Word
            && tokens.at(i).text == QStringLiteral("genes")
            && tokens.at(i + 1).kind == RulerToken::Equal
            && tokens.at(i + 2).kind == RulerToken::LBrace) {
            genesToken = i;
            break;
        }
    }
    if (genesToken < 0) {
        result.error = QStringLiteral("No genes block was found in the Ruler Designer code.");
        return result;
    }

    const int genesOpenToken = genesToken + 2;
    int genesCloseToken = -1;
    int genesDepth = 0;
    for (int tokenIndex = genesOpenToken; tokenIndex < tokens.size(); ++tokenIndex) {
        if (tokens.at(tokenIndex).kind == RulerToken::LBrace) {
            ++genesDepth;
        } else if (tokens.at(tokenIndex).kind == RulerToken::RBrace) {
            --genesDepth;
            if (genesDepth == 0) {
                genesCloseToken = tokenIndex;
                break;
            }
        }
    }

    int outerOpenToken = -1;
    for (int tokenIndex = 0; tokenIndex < genesToken; ++tokenIndex) {
        if (tokens.at(tokenIndex).kind == RulerToken::LBrace) {
            outerOpenToken = tokenIndex;
            break;
        }
    }

    int outerCloseToken = -1;
    if (outerOpenToken >= 0) {
        int outerDepth = 0;
        for (int tokenIndex = outerOpenToken; tokenIndex < tokens.size(); ++tokenIndex) {
            if (tokens.at(tokenIndex).kind == RulerToken::LBrace) {
                ++outerDepth;
            } else if (tokens.at(tokenIndex).kind == RulerToken::RBrace) {
                --outerDepth;
                if (outerDepth == 0) {
                    outerCloseToken = tokenIndex;
                    break;
                }
            }
        }
    }

    DnaData data;
    data.ruler.valid = true;
    if (outerOpenToken >= 0) {
        data.ruler.prefix = text.mid(tokens.at(outerOpenToken).end,
                                     tokens.at(genesToken).start
                                         - tokens.at(outerOpenToken).end);
    }
    if (genesCloseToken >= 0 && outerCloseToken > genesCloseToken) {
        data.ruler.suffix = text.mid(tokens.at(genesCloseToken).end,
                                     tokens.at(outerCloseToken).start
                                         - tokens.at(genesCloseToken).end);
    }

    int recognized = 0;
    int i = genesToken + 3;
    int depth = 1;

    while (i < tokens.size() && depth > 0) {
        const RulerToken &token = tokens.at(i);
        if (token.kind == RulerToken::LBrace) {
            ++depth;
            ++i;
            continue;
        }
        if (token.kind == RulerToken::RBrace) {
            --depth;
            ++i;
            continue;
        }
        if (depth != 1 || token.kind != RulerToken::Word
            || i + 2 >= tokens.size()
            || tokens.at(i + 1).kind != RulerToken::Equal
            || tokens.at(i + 2).kind != RulerToken::LBrace) {
            ++i;
            continue;
        }

        const QString key = token.text;
        i += 3;

        QVector<RulerToken> values;
        int innerDepth = 1;
        while (i < tokens.size() && innerDepth > 0) {
            const RulerToken &valueToken = tokens.at(i);
            if (valueToken.kind == RulerToken::LBrace) {
                ++innerDepth;
                ++i;
                continue;
            }
            if (valueToken.kind == RulerToken::RBrace) {
                --innerDepth;
                ++i;
                continue;
            }
            if (innerDepth == 1
                && (valueToken.kind == RulerToken::Word
                    || valueToken.kind == RulerToken::String)) {
                values.append(valueToken);
            }
            ++i;
        }

        if (values.size() != 4) {
            result.warnings.append(QStringLiteral("Skipped %1: expected 4 values, found %2.")
                                       .arg(key)
                                       .arg(values.size()));
            continue;
        }

        ++recognized;
        data.ruler.order.append(key);

        QStringList rawTokens;
        for (const RulerToken &value : values) {
            rawTokens.append(text.mid(value.start, value.end - value.start));
        }
        data.ruler.rawTokens.insert(key, rawTokens);

        const GeneDefinition *gene = catalog.byKey(key);
        if (gene && gene->inBase64) {
            if (gene->isColor()) {
                std::array<quint8, 4> color {};
                for (int v = 0; v < 4; ++v) {
                    color[v] = static_cast<quint8>(
                        byteValue(values.at(v), &result.warnings, key));
                }
                data.setColor(key, color);
            } else {
                GenePair pair;
                pair.first.variant = variantValue(values.at(0), catalog, key, &result.warnings);
                pair.first.intensity = byteValue(values.at(1), &result.warnings, key);
                pair.second.variant = variantValue(values.at(2), catalog, key, &result.warnings);
                pair.second.intensity = byteValue(values.at(3), &result.warnings, key);
                data.setGene(key, pair);
            }
            continue;
        }

        ExtraGene extra;
        extra.key = key;
        extra.kind = isRulerOnlyKey(key, catalog)
            ? ExtraGeneKind::RulerOnly
            : ExtraGeneKind::Modded;
        int firstNumber = 0;
        if (numericWord(values.at(0), &firstNumber)) {
            extra.bytes[0] = static_cast<quint8>(std::clamp(firstNumber, 0, 255));
        } else {
            extra.firstOption = values.at(0).text;
            extra.bytes[0] = 0;
        }
        extra.bytes[1] = static_cast<quint8>(
            byteValue(values.at(1), &result.warnings, key));
        int secondNumber = 0;
        if (numericWord(values.at(2), &secondNumber)) {
            extra.bytes[2] = static_cast<quint8>(std::clamp(secondNumber, 0, 255));
        } else {
            extra.secondOption = values.at(2).text;
            extra.bytes[2] = 0;
        }
        extra.bytes[3] = static_cast<quint8>(
            byteValue(values.at(3), &result.warnings, key));
        data.extras.append(extra);
    }

    if (recognized == 0) {
        result.error = QStringLiteral("The Ruler Designer code does not contain any supported genes.");
        return result;
    }

    // Fill genes missing from a partial Ruler Designer block with defaults.
    for (int index = 0; index < catalog.base64GeneCount(); ++index) {
        const GeneDefinition *gene = catalog.byBase64Index(index);
        if (!gene)
            continue;
        if (gene->isColor()) {
            if (!data.colors.contains(gene->key))
                data.setColor(gene->key, { 0, 0, 0, 0 });
        } else if (!data.genes.contains(gene->key)) {
            data.setGene(gene->key, {});
        }
    }

    int moddedExtras = 0;
    for (const ExtraGene &extra : data.extras) {
        if (extra.kind == ExtraGeneKind::Modded)
            ++moddedExtras;
    }

    result.data = std::move(data);
    result.format = DnaFormat::RulerDesigner;
    result.geneCount = catalog.base64GeneCount() + moddedExtras;
    result.ok = true;
    return result;
}

#include "core/LegacyImporter.h"

#include "core/Database.h"
#include "core/DnaCodec.h"
#include "core/GeneCatalog.h"
#include "core/ImageUtils.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace {

enum class SourceFormat
{
    Unknown,
    Legacy,
    AppExport,
};

QString valueToIdString(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble()) {
        const qint64 integer = value.toVariant().toLongLong();
        return QString::number(integer);
    }
    return {};
}

qint64 jsonTimestamp(const QJsonValue &value)
{
    if (value.isDouble())
        return static_cast<qint64>(value.toDouble());
    if (value.isString())
        return value.toString().toLongLong();
    return 0;
}

DnaParseResult parseExportDna(const QJsonObject &object,
                              const GeneCatalog &catalog,
                              const GeneCatalog &legacyCatalog,
                              QString *warning)
{
    const QString base64 = object.value(QStringLiteral("base64")).toString().trimmed();
    const QString ruler = object.value(QStringLiteral("rulerDesigner")).toString().trimmed();
    const QString rulerMeta = object.value(QStringLiteral("rulerMeta")).toString();

    DnaParseResult parsed;
    bool haveDna = false;

    if (!base64.isEmpty()) {
        parsed = DnaCodec::parse(base64, Base64Hint::Modern, catalog, legacyCatalog);
        haveDna = parsed.ok;
    }

    if (!ruler.isEmpty()) {
        const DnaParseResult rulerParsed = DnaCodec::parse(
            ruler, Base64Hint::Auto, catalog, legacyCatalog);
        if (rulerParsed.ok) {
            // Prefer the Ruler Designer representation because it also carries
            // modded keys, Ruler-only fields and the original ordering.
            parsed = rulerParsed;
            haveDna = true;
        } else if (!haveDna && warning) {
            *warning = rulerParsed.error;
        }
    }

    if (!haveDna) {
        if (warning && warning->isEmpty()) {
            *warning = parsed.error.isEmpty()
                ? QStringLiteral("no DNA payload was found")
                : parsed.error;
        }
        return {};
    }

    if (!rulerMeta.isEmpty())
        DnaCodec::applyRulerMetadata(parsed.data, rulerMeta);

    return parsed;
}

bool importExportEntry(const QJsonObject &object,
                       Database &database,
                       const GeneCatalog &catalog,
                       const GeneCatalog &legacyCatalog,
                       LegacyImportResult &result)
{
    QString name = object.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty())
        name = QStringLiteral("Imported DNA");

    QString parseWarning;
    const DnaParseResult parsed = parseExportDna(object, catalog, legacyCatalog, &parseWarning);
    if (!parsed.ok) {
        ++result.skipped;
        result.warnings.append(QStringLiteral("Skipped \"%1\": %2.")
                                   .arg(name, parseWarning));
        return false;
    }

    EntryRecord entry;
    entry.name = name;
    entry.notes = object.value(QStringLiteral("notes")).toString();
    entry.favorite = object.value(QStringLiteral("favorite")).toBool(false);
    entry.createdAt = jsonTimestamp(object.value(QStringLiteral("createdAt")));
    entry.updatedAt = jsonTimestamp(object.value(QStringLiteral("updatedAt")));
    entry.dna = DnaCodec::toBytes(parsed.data, catalog);
    entry.rulerMeta = DnaCodec::serializeRulerMetadata(parsed.data);

    const QString portraitBase64 = object.value(QStringLiteral("portraitPng")).toString().trimmed();
    if (!portraitBase64.isEmpty()) {
        const QByteArray png = QByteArray::fromBase64(portraitBase64.toLatin1());
        const QImage image = ImageUtils::imageFromBytes(png);
        if (!image.isNull()) {
            entry.portrait = ImageUtils::encodePng(image);
            entry.thumbnail = ImageUtils::makeThumbnail(image);
            ++result.portraits;
        } else {
            result.warnings.append(
                QStringLiteral("Portrait for \"%1\" could not be decoded.").arg(name));
        }
    }

    QString databaseError;
    if (!database.insert(entry, &databaseError, true)) {
        ++result.skipped;
        result.warnings.append(QStringLiteral("Skipped \"%1\": %2").arg(name, databaseError));
        return false;
    }

    ++result.imported;
    return true;
}

} // namespace

LegacyImportResult LegacyImporter::importFile(const QString &jsonPath,
                                              Database &database,
                                              const GeneCatalog &catalog,
                                              const GeneCatalog &legacyCatalog,
                                              const ProgressCallback &progress)
{
    LegacyImportResult result;

    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Cannot open the database file: %1").arg(jsonPath);
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull()) {
        result.error = QStringLiteral("Cannot parse the database at offset %1: %2")
                           .arg(parseError.offset)
                           .arg(parseError.errorString());
        return result;
    }

    SourceFormat source = SourceFormat::Unknown;
    QJsonArray items;
    if (document.isArray()) {
        source = SourceFormat::Legacy;
        items = document.array();
    } else if (document.isObject()) {
        const QJsonObject root = document.object();
        const QString format = root.value(QStringLiteral("format")).toString();

        if (format == QStringLiteral("ck3-dna-manager-qt")
            || (format.isEmpty() && root.value(QStringLiteral("entries")).isArray())) {
            source = SourceFormat::AppExport;
            const int version = root.value(QStringLiteral("version")).toInt(1);
            if (version > 1) {
                result.unsupportedVersion = true;
                result.error = QStringLiteral("The export version %1 is not supported.")
                                   .arg(version);
                return result;
            }
            const QJsonValue entries = root.value(QStringLiteral("entries"));
            if (!entries.isArray()) {
                result.unsupportedFormat = true;
                result.error = QStringLiteral("The export does not contain an entries array.");
                return result;
            }
            items = entries.toArray();
        } else if (root.value(QStringLiteral("data")).isArray()) {
            source = SourceFormat::Legacy;
            items = root.value(QStringLiteral("data")).toArray();
        }
    }

    if (source == SourceFormat::Unknown) {
        result.unsupportedFormat = true;
        result.error = QStringLiteral(
            "This file is not a supported CK3 DNA Manager database or export.");
        return result;
    }

    if (items.isEmpty()) {
        result.emptyFile = true;
        result.error = QStringLiteral("The selected file contains no entries.");
        return result;
    }

    const QDir baseDirectory = QFileInfo(jsonPath).absoluteDir();
    const QDir imageDirectory(baseDirectory.filePath(QStringLiteral("img")));

    const int total = items.size();
    for (int index = 0; index < total; ++index) {
        if (progress)
            progress(index, total);

        const QJsonObject object = items.at(index).toObject();
        if (object.isEmpty()) {
            ++result.skipped;
            continue;
        }
        if (object.contains(QStringLiteral("remove"))) {
            ++result.skipped;
            continue;
        }

        if (source == SourceFormat::AppExport) {
            importExportEntry(object, database, catalog, legacyCatalog, result);
            continue;
        }

        const QString legacyId = valueToIdString(object.value(QStringLiteral("id")));
        QString name = object.value(QStringLiteral("name")).toString().trimmed();
        const QString dnaText = object.value(QStringLiteral("dna")).toString().trimmed();
        if (name.isEmpty())
            name = QStringLiteral("Imported DNA");
        if (dnaText.isEmpty()) {
            ++result.skipped;
            result.warnings.append(QStringLiteral("Skipped \"%1\": empty DNA.").arg(name));
            continue;
        }

        const DnaParseResult parsed = DnaCodec::parse(
            dnaText, Base64Hint::Legacy, catalog, legacyCatalog);
        if (!parsed.ok) {
            ++result.skipped;
            result.warnings.append(QStringLiteral("Skipped \"%1\": %2").arg(name, parsed.error));
            continue;
        }

        EntryRecord entry;
        entry.name = name;
        entry.dna = DnaCodec::toBytes(parsed.data, catalog);
        entry.rulerMeta = DnaCodec::serializeRulerMetadata(parsed.data);
        const qint64 createdHint = legacyId.toLongLong();
        entry.createdAt = createdHint > 946684800000LL
            ? createdHint
            : QDateTime::currentMSecsSinceEpoch();

        if (!legacyId.isEmpty()) {
            QStringList portraitNames;
            for (const QString &suffix : ImageUtils::supportedImageSuffixes()) {
                portraitNames.append(legacyId + QLatin1Char('.') + suffix);
            }

            QString portraitPath;
            for (const QString &candidate : portraitNames) {
                const QString path = imageDirectory.filePath(candidate);
                if (QFileInfo::exists(path)) {
                    portraitPath = path;
                    break;
                }
            }

            if (!portraitPath.isEmpty()) {
                QString imageError;
                const QImage image = ImageUtils::loadImage(portraitPath, &imageError);
                if (!image.isNull()) {
                    entry.portrait = ImageUtils::encodePng(image);
                    entry.thumbnail = ImageUtils::makeThumbnail(image);
                    ++result.portraits;
                } else {
                    result.warnings.append(QStringLiteral("Portrait for \"%1\": %2")
                                               .arg(name, imageError));
                }
            }
        }

        QString databaseError;
        if (!database.insert(entry, &databaseError, true)) {
            ++result.skipped;
            result.warnings.append(QStringLiteral("Skipped \"%1\": %2").arg(name, databaseError));
            continue;
        }
        ++result.imported;
    }

    if (progress)
        progress(total, total);

    result.ok = true;
    return result;
}

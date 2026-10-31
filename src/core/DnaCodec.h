#pragma once

#include "core/DnaData.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

class GeneCatalog;

enum class DnaFormat
{
    Invalid,
    ModernBase64,
    LegacyBase64,
    RulerDesigner,
};

enum class Base64Hint
{
    Auto,
    Modern,
    Legacy,
};

struct DnaParseResult
{
    bool ok = false;
    QString error;
    QStringList warnings;
    DnaData data;
    DnaFormat format = DnaFormat::Invalid;
    bool legacyConverted = false;
    int geneCount = 0;
};

/// Conversion between CK3 Base64 DNA, Ruler Designer text and DnaData.
class DnaCodec
{
public:
    /// Parse pasted text. Ruler Designer input is detected automatically.
    static DnaParseResult parse(const QString &text,
                                Base64Hint hint,
                                const GeneCatalog &catalog,
                                const GeneCatalog &legacyCatalog);

    /// Parse a raw 4-byte-per-gene payload (used for values loaded from the DB).
    static DnaParseResult fromBytes(const QByteArray &bytes,
                                    Base64Hint hint,
                                    const GeneCatalog &catalog,
                                    const GeneCatalog &legacyCatalog);

    static QByteArray toBytes(const DnaData &data, const GeneCatalog &catalog);
    static QString toBase64(const DnaData &data, const GeneCatalog &catalog);
    static QString toRuler(const DnaData &data, const GeneCatalog &catalog);

    /// Persist and restore the Ruler Designer fields that are not represented
    /// by the canonical 104-gene Base64 payload.
    static QString serializeRulerMetadata(const DnaData &data);
    static void applyRulerMetadata(DnaData &data, const QString &json);

    static bool looksLikeLegacy(const QByteArray &bytes, const GeneCatalog &catalog);

private:
    static DnaParseResult parseBase64Bytes(const QByteArray &bytes,
                                           Base64Hint hint,
                                           const GeneCatalog &catalog,
                                           const GeneCatalog &legacyCatalog);
    static DnaParseResult parseRuler(const QString &text,
                                     const GeneCatalog &catalog,
                                     const GeneCatalog &legacyCatalog);
    static DnaData convertLegacy(const DnaData &legacy,
                                 const GeneCatalog &catalog,
                                 const GeneCatalog &legacyCatalog,
                                 QStringList *warnings);
};

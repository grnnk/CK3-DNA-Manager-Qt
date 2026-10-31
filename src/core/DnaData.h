#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>

/// One allele of a morph/detail gene: a variant index plus its strength.
struct GeneAllele
{
    int variant = 0;
    int intensity = 0;

    friend bool operator==(const GeneAllele &, const GeneAllele &) = default;
};

/// A gene has two alleles in the DNA payload (the "current" and "inherit" values).
struct GenePair
{
    GeneAllele first;
    GeneAllele second;

    friend bool operator==(const GenePair &, const GenePair &) = default;
};

enum class ExtraGeneKind
{
    /// Unknown to the bundled catalog; appended to the Base64 payload.
    Modded,
    /// A vanilla Ruler Designer field that is not part of the Base64 payload.
    RulerOnly,
};

/// A gene outside the 104 vanilla Base64 slots. Modded genes are appended to
/// the payload; Ruler-only genes live only in the Ruler Designer metadata.
struct ExtraGene
{
    QString key;
    std::array<quint8, 4> bytes {};
    ExtraGeneKind kind = ExtraGeneKind::Modded;

    /// Option names found in an imported Ruler Designer file, when available.
    QString firstOption;
    QString secondOption;

    friend bool operator==(const ExtraGene &, const ExtraGene &) = default;
};

/// Formatting and ordering information retained from a pasted Ruler Designer
/// block. The canonical DNA stays in DnaData::genes/colors/extras; this only
/// preserves the non-vanilla representation when the entry is copied again.
struct RulerMetadata
{
    bool valid = false;
    QString prefix;
    QString suffix;
    QStringList order;

    /// Original token text for vanilla genes. A key is removed as soon as the
    /// corresponding gene is edited so regenerated output uses current values.
    QMap<QString, QStringList> rawTokens;

    friend bool operator==(const RulerMetadata &, const RulerMetadata &) = default;
};

/// Format-independent representation of one CK3 DNA payload.
///
/// Vanilla base64 DNA contains 104 genes, four bytes each:
///   [allele1 variant, allele1 strength, allele2 variant, allele2 strength]
/// The three colour genes store four raw values instead of allele pairs.
class DnaData
{
public:
    QMap<QString, GenePair> genes;
    QMap<QString, std::array<quint8, 4>> colors;
    QVector<ExtraGene> extras;
    RulerMetadata ruler;

    GenePair gene(const QString &key) const { return genes.value(key); }
    void setGene(const QString &key, const GenePair &value) { genes.insert(key, value); }

    std::array<quint8, 4> color(const QString &key) const
    {
        return colors.value(key, { 0, 0, 0, 0 });
    }
    void setColor(const QString &key, const std::array<quint8, 4> &value)
    {
        colors.insert(key, value);
    }

    int extraCount() const { return extras.size(); }
    bool operator==(const DnaData &) const = default;
};

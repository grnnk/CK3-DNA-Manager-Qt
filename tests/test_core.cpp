#include "core/Database.h"
#include "core/DnaCodec.h"
#include "core/DnaData.h"
#include "core/GeneCatalog.h"
#include "core/I18n.h"
#include "core/ImageUtils.h"
#include "core/LegacyImporter.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

namespace {

constexpr int kModernGeneCount = 104;
constexpr int kBytesPerGene = 4;

DnaData sampleData(const GeneCatalog &catalog)
{
    DnaData data;
    for (int slot = 0; slot < catalog.base64GeneCount(); ++slot) {
        const GeneDefinition *gene = catalog.byBase64Index(slot);
        Q_ASSERT(gene);

        if (gene->isColor()) {
            data.setColor(gene->key,
                          { static_cast<quint8>(10 + slot),
                            static_cast<quint8>(20 + slot),
                            static_cast<quint8>(30 + slot),
                            static_cast<quint8>(40 + slot) });
            continue;
        }

        const int optionCount = qMax(1, static_cast<int>(gene->options.size()));
        GenePair pair;
        pair.first.variant = slot % optionCount;
        pair.first.intensity = (slot * 3) % 256;
        pair.second.variant = (slot + 1) % optionCount;
        pair.second.intensity = (slot * 5) % 256;
        data.setGene(gene->key, pair);
    }
    return data;
}

QByteArray legacyPayload()
{
    QByteArray bytes(kModernGeneCount * kBytesPerGene, '\0');

    // complexion: legacy option 7 = complexion_beauty_1 (modern index 8)
    bytes[90 * 4 + 0] = static_cast<char>(7);
    bytes[90 * 4 + 1] = static_cast<char>(120);
    bytes[90 * 4 + 2] = static_cast<char>(9); // complexion_no_face (modern index 10)
    bytes[90 * 4 + 3] = static_cast<char>(200);

    // bust: legacy option 9 = bust_shape_4_full (modern index 10)
    bytes[94 * 4 + 0] = static_cast<char>(9);
    bytes[94 * 4 + 1] = static_cast<char>(30);
    bytes[94 * 4 + 2] = static_cast<char>(4); // bust_shape_2_half (modern index 5)
    bytes[94 * 4 + 3] = static_cast<char>(60);

    // Legacy hairstyles/beards values that cannot be modern hair type/baldness.
    bytes[99 * 4 + 0] = static_cast<char>(7);
    bytes[100 * 4 + 0] = static_cast<char>(5);

    return bytes;
}

QString writeJsonFile(const QTemporaryDir &directory,
                      const QString &fileName,
                      const QJsonDocument &document)
{
    const QString path = directory.filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(document.toJson(QJsonDocument::Indented));
    file.close();
    return path;
}

} // namespace

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void catalogStructure();
    void catalogLookups();

    void modernRoundTrip();
    void modernSlotLayout();
    void moddedRoundTrip();

    void legacyIsConvertedById();
    void legacyAutoDetection();

    void rulerRoundTrip();
    void rulerPartialCode();
    void rulerPreservesRulerOnlyGenes();
    void rulerPreservesModdedKeys();
    void rulerOnlyCodeIsValid();

    void databaseLifecycle();
    void databaseAllowsEmptyNotes();
    void databaseRejectsEmptyDna();
    void databaseStoresRulerMetadata();
    void databaseMigratesRulerMetadata();

    void importAppExport();
    void importRejectsUnsupportedFiles();
    void importPreservesLegacyFormat();

    void translationsAreComplete();
    void translationFallback();

    void thumbnailIsCropped();
    void imageFormatSupport();
    void webPImageDecodes();

private:
    GeneCatalog m_catalog;
    GeneCatalog m_legacyCatalog;
};

void TestCore::initTestCase()
{
    QString error;
    QVERIFY2(m_catalog.load(QStringLiteral(":/data/genes.json"), &error),
             qPrintable(error));
    QVERIFY2(m_legacyCatalog.load(QStringLiteral(":/data/legacy_genes.json"), &error),
             qPrintable(error));
}

void TestCore::catalogStructure()
{
    QVERIFY(m_catalog.isLoaded());
    QCOMPARE(m_catalog.base64GeneCount(), kModernGeneCount);
    QCOMPARE(m_catalog.genes().size(), 106);
    QCOMPARE(m_catalog.categoryOrder().size(), 12);

    QSet<int> indexes;
    for (const GeneDefinition &gene : m_catalog.genes()) {
        QVERIFY(!gene.key.isEmpty());
        QVERIFY(!gene.name.isEmpty());
        QVERIFY(!gene.category.isEmpty());
        if (gene.inBase64) {
            QVERIFY(gene.base64Index >= 0);
            QVERIFY(gene.base64Index < kModernGeneCount);
            QVERIFY(!indexes.contains(gene.base64Index));
            indexes.insert(gene.base64Index);
        }
        if (!gene.isColor()) {
            QVERIFY(!gene.options.isEmpty());
        }
    }
    QCOMPARE(indexes.size(), kModernGeneCount);
}

void TestCore::catalogLookups()
{
    QCOMPARE(m_catalog.byBase64Index(0)->key, QStringLiteral("hair_color"));
    QCOMPARE(m_catalog.byBase64Index(1)->key, QStringLiteral("skin_color"));
    QCOMPARE(m_catalog.byBase64Index(2)->key, QStringLiteral("eye_color"));
    QCOMPARE(m_catalog.byBase64Index(99)->key, QStringLiteral("gene_hair_type"));
    QCOMPARE(m_catalog.byBase64Index(100)->key, QStringLiteral("gene_baldness"));
    QCOMPARE(m_catalog.byBase64Index(101)->key, QStringLiteral("eye_accessory"));
    QCOMPARE(m_catalog.byBase64Index(102)->key, QStringLiteral("teeth_accessory"));
    QCOMPARE(m_catalog.byBase64Index(103)->key, QStringLiteral("eyelashes_accessory"));
    QVERIFY(m_catalog.byBase64Index(104) == nullptr);
    QVERIFY(m_catalog.byKey(QStringLiteral("does_not_exist")) == nullptr);

    const GeneDefinition *hair = m_catalog.byKey(QStringLiteral("gene_hair_type"));
    QVERIFY(hair);
    QCOMPARE(hair->options.size(), 5);
    QCOMPARE(hair->options.first().id, QStringLiteral("hair_straight"));
    QCOMPARE(m_catalog.optionIndexById(hair->key, QStringLiteral("hair_curly")), 2);
    QCOMPARE(m_catalog.optionId(hair->key, 4), QStringLiteral("hair_straight_thin_beard"));

    const GeneDefinition *eyes = m_catalog.byKey(QStringLiteral("eye_accessory"));
    QVERIFY(eyes);
    QCOMPARE(eyes->options.first().id, QStringLiteral("no_eyes"));
    QCOMPARE(eyes->options.first().index, 0);
    QCOMPARE(m_catalog.optionIndexById(QStringLiteral("eye_accessory"),
                                       QStringLiteral("normal_eyes")), 1);

    // These indices are the bytes the game stores; they were shifted by one in
    // older catalogs, which made normal eyes render as no_eyes in game.
    const GeneDefinition *teeth = m_catalog.byKey(QStringLiteral("teeth_accessory"));
    QVERIFY(teeth);
    QCOMPARE(m_catalog.optionIndexById(QStringLiteral("teeth_accessory"),
                                       QStringLiteral("no_teeth")), 0);
    QCOMPARE(m_catalog.optionIndexById(QStringLiteral("teeth_accessory"),
                                       QStringLiteral("normal_teeth")), 1);
    QCOMPARE(m_catalog.optionIndexById(QStringLiteral("teeth_accessory"),
                                       QStringLiteral("ohaguro_teeth")), 2);

    // The two eye alleles must encode 1 (normal_eyes), not 0 (no_eyes).
    GenePair normalEyes;
    normalEyes.first.variant = 1;
    normalEyes.second.variant = 1;
    DnaData eyeData;
    eyeData.setGene(QStringLiteral("eye_accessory"), normalEyes);
    const QByteArray eyeBytes = DnaCodec::toBytes(eyeData, m_catalog);
    QCOMPARE(eyeBytes.size(), kModernGeneCount * kBytesPerGene);
    QCOMPARE(static_cast<quint8>(eyeBytes.at(101 * kBytesPerGene)), quint8(1));
    QCOMPARE(static_cast<quint8>(eyeBytes.at(101 * kBytesPerGene + 2)), quint8(1));

    QCOMPARE(GeneCatalog::categoryTranslationKey(QStringLiteral("face")),
             QStringLiteral("genes.category.face"));
}

void TestCore::modernRoundTrip()
{
    const DnaData data = sampleData(m_catalog);
    const QByteArray bytes = DnaCodec::toBytes(data, m_catalog);
    QCOMPARE(bytes.size(), kModernGeneCount * kBytesPerGene);

    const DnaParseResult parsed = DnaCodec::fromBytes(
        bytes, Base64Hint::Modern, m_catalog, m_legacyCatalog);
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(static_cast<int>(parsed.format), static_cast<int>(DnaFormat::ModernBase64));
    QCOMPARE(parsed.geneCount, kModernGeneCount);
    QVERIFY(!parsed.legacyConverted);
    QVERIFY(parsed.data == data);

    const QString base64 = DnaCodec::toBase64(data, m_catalog);
    QCOMPARE(base64, QString::fromLatin1(bytes.toBase64()));

    const DnaParseResult reparsed = DnaCodec::parse(
        base64, Base64Hint::Auto, m_catalog, m_legacyCatalog);
    QVERIFY2(reparsed.ok, qPrintable(reparsed.error));
    QVERIFY(reparsed.data == data);
}

void TestCore::modernSlotLayout()
{
    const DnaData data = sampleData(m_catalog);
    const QByteArray bytes = DnaCodec::toBytes(data, m_catalog);
    QVERIFY(!DnaCodec::looksLikeLegacy(bytes, m_catalog));

    const DnaParseResult parsed = DnaCodec::fromBytes(
        bytes, Base64Hint::Auto, m_catalog, m_legacyCatalog);
    QVERIFY(parsed.ok);
    QVERIFY(!parsed.legacyConverted);
    QCOMPARE(static_cast<int>(parsed.format), static_cast<int>(DnaFormat::ModernBase64));

    const GeneDefinition *hair = m_catalog.byBase64Index(99);
    const GeneDefinition *baldness = m_catalog.byBase64Index(100);
    QVERIFY(hair);
    QVERIFY(baldness);
    QCOMPARE(parsed.data.gene(hair->key).first.variant,
             data.gene(hair->key).first.variant);
    QCOMPARE(parsed.data.gene(baldness->key).first.variant,
             data.gene(baldness->key).first.variant);
}

void TestCore::moddedRoundTrip()
{
    DnaData data = sampleData(m_catalog);
    for (int index = 0; index < 13; ++index) {
        ExtraGene extra;
        extra.key = QStringLiteral("extra_%1").arg(kModernGeneCount + 1 + index);
        extra.bytes = { static_cast<quint8>(200 + index),
                        static_cast<quint8>(index),
                        static_cast<quint8>(100 + index),
                        static_cast<quint8>(50 + index) };
        data.extras.append(extra);
    }

    const QByteArray bytes = DnaCodec::toBytes(data, m_catalog);
    QCOMPARE(bytes.size(), (kModernGeneCount + 13) * kBytesPerGene);
    QCOMPARE(bytes.toBase64().size(), 624);

    const QString code = DnaCodec::toBase64(data, m_catalog);
    QCOMPARE(code.size(), 624);

    const DnaParseResult parsed = DnaCodec::parse(
        code, Base64Hint::Auto, m_catalog, m_legacyCatalog);
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(parsed.geneCount, kModernGeneCount + 13);
    QCOMPARE(parsed.data.extras.size(), 13);
    QVERIFY(parsed.data == data);

    QCOMPARE(parsed.data.extras.first().key, QStringLiteral("extra_105"));
    QCOMPARE(static_cast<int>(parsed.data.extras.first().bytes[0]), 200);
    QCOMPARE(static_cast<int>(parsed.data.extras.last().bytes[3]), 62);
}

void TestCore::legacyIsConvertedById()
{
    const QByteArray legacy = legacyPayload();
    const DnaParseResult parsed = DnaCodec::fromBytes(
        legacy, Base64Hint::Legacy, m_catalog, m_legacyCatalog);
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(static_cast<int>(parsed.format), static_cast<int>(DnaFormat::LegacyBase64));
    QVERIFY(parsed.legacyConverted);

    const GenePair complexion = parsed.data.gene(QStringLiteral("complexion"));
    QCOMPARE(complexion.first.variant, 8);
    QCOMPARE(complexion.first.intensity, 120);
    QCOMPARE(complexion.second.variant, 10);
    QCOMPARE(complexion.second.intensity, 200);

    const GenePair bust = parsed.data.gene(QStringLiteral("gene_bs_bust"));
    QCOMPARE(bust.first.variant, 10);
    QCOMPARE(bust.first.intensity, 30);
    QCOMPARE(bust.second.variant, 5);
    QCOMPARE(bust.second.intensity, 60);

    // Legacy hair/beard groups have no modern equivalent.
    QCOMPARE(parsed.data.gene(QStringLiteral("gene_hair_type")).first.variant, 0);
    QCOMPARE(parsed.data.gene(QStringLiteral("gene_baldness")).first.variant, 0);
    QCOMPARE(parsed.data.extras.size(), 0);
}

void TestCore::legacyAutoDetection()
{
    const QByteArray legacy = legacyPayload();
    QVERIFY(DnaCodec::looksLikeLegacy(legacy, m_catalog));

    const DnaParseResult parsed = DnaCodec::fromBytes(
        legacy, Base64Hint::Auto, m_catalog, m_legacyCatalog);
    QVERIFY(parsed.ok);
    QVERIFY(parsed.legacyConverted);
    QCOMPARE(parsed.data.gene(QStringLiteral("complexion")).first.variant, 8);

    // A legacy payload with 13 extra slots is ambiguous; it stays modern.
    QByteArray extended = legacy;
    extended.append(QByteArray(13 * kBytesPerGene, '\x05'));
    const DnaParseResult modern = DnaCodec::fromBytes(
        extended, Base64Hint::Auto, m_catalog, m_legacyCatalog);
    QVERIFY(modern.ok);
    QVERIFY(!modern.legacyConverted);
    QCOMPARE(static_cast<int>(modern.format), static_cast<int>(DnaFormat::ModernBase64));
    QCOMPARE(modern.data.extras.size(), 13);
}

void TestCore::rulerRoundTrip()
{
    DnaData data = sampleData(m_catalog);
    ExtraGene extra;
    extra.key = QStringLiteral("mod_gene_x");
    extra.bytes = { 5, 77, 9, 88 };
    data.extras.append(extra);

    const QString ruler = DnaCodec::toRuler(data, m_catalog);
    QVERIFY(ruler.contains(QStringLiteral("genes={")));
    QVERIFY(ruler.contains(QStringLiteral("gene_hair_type={")));
    QVERIFY(ruler.contains(QStringLiteral("mod_gene_x={")));

    const DnaParseResult parsed = DnaCodec::parse(
        ruler, Base64Hint::Auto, m_catalog, m_legacyCatalog);
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(static_cast<int>(parsed.format), static_cast<int>(DnaFormat::RulerDesigner));
    QCOMPARE(parsed.data.extras.size(), 1);
    QVERIFY(parsed.data.genes == data.genes);
    QVERIFY(parsed.data.colors == data.colors);
    QVERIFY(parsed.data.extras == data.extras);
    QVERIFY(parsed.data.ruler.valid);
    QVERIFY(parsed.data.ruler.order.contains(QStringLiteral("mod_gene_x")));
    QVERIFY(parsed.data.ruler.rawTokens.contains(QStringLiteral("gene_hair_type")));
}

void TestCore::rulerPartialCode()
{
    const QString text = QStringLiteral(
        "ruler_designer_123456789={\n"
        "\tgenes={\n"
        "\t\tgene_hair_type={ \"hair_wavy\" 100 \"hair_curly\" 50 }\n"
        "\t}\n"
        "}\n");

    const DnaParseResult parsed = DnaCodec::parse(
        text, Base64Hint::Auto, m_catalog, m_legacyCatalog);
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(static_cast<int>(parsed.format), static_cast<int>(DnaFormat::RulerDesigner));

    const GenePair hair = parsed.data.gene(QStringLiteral("gene_hair_type"));
    QCOMPARE(hair.first.variant, 1);
    QCOMPARE(hair.first.intensity, 100);
    QCOMPARE(hair.second.variant, 2);
    QCOMPARE(hair.second.intensity, 50);

    // Missing genes are filled with defaults so the payload can be encoded.
    QCOMPARE(parsed.data.genes.size(), m_catalog.base64GeneCount()
             - 3); // three colour genes live in `colors`
    QCOMPARE(parsed.data.colors.size(), 3);
}

void TestCore::rulerPreservesRulerOnlyGenes()
{
    const QString text = QStringLiteral(
        "ruler_designer_42={\n"
        "\ttype=female\n"
        "\tgenes={\n"
        "\t\tgene_baldness={ male_pattern_baldness 80 no_baldness 0 }\n"
        "\t\tbeards={ no_beard 255 no_beard 0 }\n"
        "\t\thairstyles={ western_hairstyles 0 western_hairstyles 0 }\n"
        "\t\tclothes={ no_clothes 202 most_clothes 0 }\n"
        "\t}\n"
        "\toverride={ portrait_modifier_overrides={ custom_beards=female_empty "
        "custom_hair=female_hair_fp1_04 } }\n"
        "\tentity={ 0 0 }\n"
        "}\n");

    const DnaParseResult parsed = DnaCodec::parse(
        text, Base64Hint::Auto, m_catalog, m_legacyCatalog);
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(parsed.data.extras.size(), 3);
    for (const ExtraGene &extra : parsed.data.extras) {
        QVERIFY(extra.kind == ExtraGeneKind::RulerOnly);
    }
    QCOMPARE(parsed.data.extras.at(0).key, QStringLiteral("beards"));
    QCOMPARE(parsed.data.extras.at(1).key, QStringLiteral("hairstyles"));
    QCOMPARE(parsed.data.extras.at(2).key, QStringLiteral("clothes"));

    // Ruler-only genes must not be appended to the Base64 payload.
    const QByteArray bytes = DnaCodec::toBytes(parsed.data, m_catalog);
    QCOMPARE(bytes.size(), kModernGeneCount * kBytesPerGene);

    const QString ruler = DnaCodec::toRuler(parsed.data, m_catalog);
    QVERIFY(ruler.contains(QStringLiteral("beards={ no_beard 255 no_beard 0 }")));
    QVERIFY(ruler.contains(QStringLiteral("clothes={ no_clothes 202 most_clothes 0 }")));
    QVERIFY(ruler.contains(QStringLiteral("type=female")));
    QVERIFY(ruler.contains(QStringLiteral("override=")));
    QVERIFY(ruler.contains(QStringLiteral("custom_hair=female_hair_fp1_04")));
    QVERIFY(ruler.contains(QStringLiteral("entity={ 0 0 }")));
    QVERIFY(ruler.contains(QStringLiteral("male_pattern_baldness 80")));
}

void TestCore::rulerPreservesModdedKeys()
{
    const QString text = QStringLiteral(
        "ruler_designer_7={\n"
        "\tgenes={\n"
        "\t\tgene_hair_type={ hair_wavy 100 hair_curly 50 }\n"
        "\t\tgene_bs_butt_size={ butt_size 135 butt_size_narrow_hips 51 }\n"
        "\t\tmod_gene_alpha={ 5 77 9 88 }\n"
        "\t}\n"
        "}\n");

    const DnaParseResult parsed = DnaCodec::parse(
        text, Base64Hint::Auto, m_catalog, m_legacyCatalog);
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(parsed.data.extras.size(), 2);
    QVERIFY(parsed.data.extras.at(0).kind == ExtraGeneKind::Modded);
    QCOMPARE(parsed.data.extras.at(0).key, QStringLiteral("gene_bs_butt_size"));
    QCOMPARE(parsed.data.extras.at(0).firstOption, QStringLiteral("butt_size"));
    QCOMPARE(static_cast<int>(parsed.data.extras.at(0).bytes[1]), 135);
    QCOMPARE(parsed.data.extras.at(1).key, QStringLiteral("mod_gene_alpha"));

    const QByteArray bytes = DnaCodec::toBytes(parsed.data, m_catalog);
    QCOMPARE(bytes.size(), (kModernGeneCount + 2) * kBytesPerGene);

    const QString metadata = DnaCodec::serializeRulerMetadata(parsed.data);
    QVERIFY(!metadata.isEmpty());

    const DnaParseResult loaded = DnaCodec::fromBytes(
        bytes, Base64Hint::Modern, m_catalog, m_legacyCatalog);
    QVERIFY2(loaded.ok, qPrintable(loaded.error));
    DnaData restored = loaded.data;
    DnaCodec::applyRulerMetadata(restored, metadata);
    QCOMPARE(restored.extras.size(), 2);
    QCOMPARE(restored.extras.at(0).key, QStringLiteral("gene_bs_butt_size"));
    QCOMPARE(restored.extras.at(0).firstOption, QStringLiteral("butt_size"));
    QCOMPARE(restored.extras.at(1).key, QStringLiteral("mod_gene_alpha"));

    const QString ruler = DnaCodec::toRuler(restored, m_catalog);
    QVERIFY(ruler.contains(
        QStringLiteral("gene_bs_butt_size={ butt_size 135 butt_size_narrow_hips 51 }")));
    QVERIFY(ruler.contains(QStringLiteral("mod_gene_alpha={ 5 77 9 88 }")));
    QVERIFY(ruler.contains(QStringLiteral("hair_wavy 100")));
    QCOMPARE(DnaCodec::toBytes(restored, m_catalog), bytes);
}

void TestCore::rulerOnlyCodeIsValid()
{
    const QString text = QStringLiteral(
        "ruler_designer_9={\n"
        "\tgenes={\n"
        "\t\tmod_gene_only={ \"alpha\" 10 \"beta\" 20 }\n"
        "\t\tclothes={ no_clothes 0 most_clothes 0 }\n"
        "\t}\n"
        "}\n");

    const DnaParseResult parsed = DnaCodec::parse(
        text, Base64Hint::Auto, m_catalog, m_legacyCatalog);
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(parsed.data.extras.size(), 2);
    QVERIFY(parsed.data.extras.at(0).kind == ExtraGeneKind::Modded);
    QVERIFY(parsed.data.extras.at(1).kind == ExtraGeneKind::RulerOnly);
    QCOMPARE(parsed.geneCount, kModernGeneCount + 1);
    QCOMPARE(DnaCodec::toBytes(parsed.data, m_catalog).size(),
             (kModernGeneCount + 1) * kBytesPerGene);
    QVERIFY(DnaCodec::toRuler(parsed.data, m_catalog)
                .contains(QStringLiteral("mod_gene_only")));
}

void TestCore::databaseLifecycle()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("dna.db"));

    Database database;
    QString error;
    QVERIFY2(database.open(path, &error), qPrintable(error));
    QVERIFY(database.isOpen());
    QCOMPARE(database.filePath(), path);

    EntryRecord entry;
    entry.name = QStringLiteral("Test entry");
    entry.notes = QStringLiteral("A note");
    entry.dna = QByteArray(kModernGeneCount * kBytesPerGene, '\x01');
    entry.favorite = true;
    entry.createdAt = QDateTime::currentMSecsSinceEpoch();
    QVERIFY2(database.insert(entry, &error), qPrintable(error));
    QVERIFY(!entry.id.isEmpty());
    QVERIFY(entry.createdAt > 0);

    QVector<EntryRecord> entries = database.entries(&error);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().id, entry.id);
    QCOMPARE(entries.first().name, entry.name);
    QCOMPARE(entries.first().notes, entry.notes);
    QCOMPARE(entries.first().dna, entry.dna);
    QVERIFY(entries.first().favorite);
    QVERIFY(!entries.first().hasPortrait());

    EntryRecord loaded;
    QVERIFY2(database.loadEntry(entry.id, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.dna, entry.dna);

    loaded.name = QStringLiteral("Renamed");
    loaded.favorite = false;
    loaded.portrait = QByteArray("portrait");
    loaded.thumbnail = QByteArray("thumb");
    QVERIFY2(database.update(loaded, &error), qPrintable(error));

    // Closing and reopening should preserve the record.
    database.close();
    QVERIFY(!database.isOpen());
    QVERIFY2(database.open(path, &error), qPrintable(error));

    EntryRecord reloaded;
    QVERIFY2(database.loadEntry(entry.id, &reloaded, &error), qPrintable(error));
    QCOMPARE(reloaded.name, QStringLiteral("Renamed"));
    QVERIFY(!reloaded.favorite);
    QCOMPARE(reloaded.portrait, QByteArray("portrait"));
    QCOMPARE(reloaded.thumbnail, QByteArray("thumb"));

    QVERIFY2(database.remove(entry.id, &error), qPrintable(error));
    QCOMPARE(database.entries(&error).size(), 0);

    EntryRecord missing;
    QVERIFY(!database.loadEntry(entry.id, &missing, &error));
    QVERIFY(!error.isEmpty());
    database.close();
}

void TestCore::databaseAllowsEmptyNotes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    Database database;
    QString error;
    QVERIFY2(database.open(directory.filePath(QStringLiteral("dna.db")), &error),
             qPrintable(error));

    // A freshly constructed EntryRecord leaves notes empty; it must still insert.
    EntryRecord entry;
    entry.name = QStringLiteral("No notes");
    entry.dna = QByteArray(kModernGeneCount * kBytesPerGene, '\x02');
    QVERIFY2(database.insert(entry, &error), qPrintable(error));

    EntryRecord loaded;
    QVERIFY2(database.loadEntry(entry.id, &loaded, &error), qPrintable(error));
    QVERIFY(loaded.notes.isEmpty());

    // An explicit null QString must not violate the NOT NULL constraint either.
    loaded.notes = QString();
    QVERIFY2(database.update(loaded, &error), qPrintable(error));

    EntryRecord reloaded;
    QVERIFY2(database.loadEntry(entry.id, &reloaded, &error), qPrintable(error));
    QVERIFY(reloaded.notes.isEmpty());

    database.close();
}

void TestCore::databaseRejectsEmptyDna()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    Database database;
    QString error;
    QVERIFY2(database.open(directory.filePath(QStringLiteral("dna.db")), &error),
             qPrintable(error));

    EntryRecord entry;
    entry.name = QStringLiteral("Empty");
    QVERIFY(!database.insert(entry, &error));
    QVERIFY(!error.isEmpty());

    QVERIFY(database.entries(&error).isEmpty());
}

void TestCore::databaseStoresRulerMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    Database database;
    QString error;
    QVERIFY2(database.open(directory.filePath(QStringLiteral("dna.db")), &error),
             qPrintable(error));

    const QString metadata = QStringLiteral(
        "{\"format\":\"ck3-ruler-meta\",\"version\":1,"
        "\"order\":[\"mod_gene_alpha\"],"
        "\"extras\":[{\"key\":\"mod_gene_alpha\",\"kind\":\"modded\","
        "\"bytes\":[5,77,9,88]}]}");

    EntryRecord entry;
    entry.name = QStringLiteral("Ruler metadata");
    entry.dna = QByteArray(kModernGeneCount * kBytesPerGene, '\0');
    entry.rulerMeta = metadata;
    QVERIFY2(database.insert(entry, &error), qPrintable(error));

    EntryRecord loaded;
    QVERIFY2(database.loadEntry(entry.id, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.rulerMeta, metadata);

    loaded.rulerMeta = QStringLiteral("{\"format\":\"ck3-ruler-meta\",\"version\":1}");
    QVERIFY2(database.update(loaded, &error), qPrintable(error));

    EntryRecord reloaded;
    QVERIFY2(database.loadEntry(entry.id, &reloaded, &error), qPrintable(error));
    QCOMPARE(reloaded.rulerMeta, loaded.rulerMeta);

    database.close();
}

void TestCore::databaseMigratesRulerMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("old.db"));
    const QString connectionName = QStringLiteral("ck3_old_schema_test");

    {
        QSqlDatabase oldDatabase = QSqlDatabase::addDatabase(
            QStringLiteral("QSQLITE"), connectionName);
        oldDatabase.setDatabaseName(path);
        QVERIFY(oldDatabase.open());

        QSqlQuery query(oldDatabase);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE entries ("
            "id TEXT PRIMARY KEY, name TEXT NOT NULL, notes TEXT NOT NULL DEFAULT '', "
            "dna BLOB NOT NULL, portrait BLOB, thumbnail BLOB, "
            "favorite INTEGER NOT NULL DEFAULT 0, "
            "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL)")));
        query.prepare(QStringLiteral(
            "INSERT INTO entries "
            "(id, name, notes, dna, portrait, thumbnail, favorite, created_at, updated_at) "
            "VALUES ('old', 'Old entry', '', :dna, NULL, NULL, 0, 1, 1)"));
        query.bindValue(QStringLiteral(":dna"),
                        QByteArray(kModernGeneCount * kBytesPerGene, '\0'));
        QVERIFY(query.exec());
        oldDatabase.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    Database database;
    QString error;
    QVERIFY2(database.open(path, &error), qPrintable(error));

    EntryRecord oldEntry;
    QVERIFY2(database.loadEntry(QStringLiteral("old"), &oldEntry, &error), qPrintable(error));
    QVERIFY(oldEntry.rulerMeta.isEmpty());

    EntryRecord entry;
    entry.name = QStringLiteral("After migration");
    entry.dna = QByteArray(kModernGeneCount * kBytesPerGene, '\0');
    entry.rulerMeta = QStringLiteral("{\"format\":\"ck3-ruler-meta\",\"version\":1}");
    QVERIFY2(database.insert(entry, &error), qPrintable(error));

    EntryRecord loaded;
    QVERIFY2(database.loadEntry(entry.id, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.rulerMeta, entry.rulerMeta);

    database.close();
}

void TestCore::importAppExport()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    Database database;
    QString error;
    QVERIFY2(database.open(directory.filePath(QStringLiteral("dna.db")), &error),
             qPrintable(error));

    DnaData data = sampleData(m_catalog);
    ExtraGene extra;
    extra.key = QStringLiteral("mod_gene_x");
    extra.bytes = { 5, 77, 9, 88 };
    data.extras.append(extra);

    const QString ruler = DnaCodec::toRuler(data, m_catalog);
    const DnaParseResult rulerParsed = DnaCodec::parse(
        ruler, Base64Hint::Auto, m_catalog, m_legacyCatalog);
    QVERIFY2(rulerParsed.ok, qPrintable(rulerParsed.error));
    const QByteArray dna = DnaCodec::toBytes(rulerParsed.data, m_catalog);
    const QString metadata = DnaCodec::serializeRulerMetadata(rulerParsed.data);
    QVERIFY(!metadata.isEmpty());

    QImage portrait(120, 160, QImage::Format_ARGB32);
    portrait.fill(Qt::darkCyan);
    const QByteArray portraitPng = ImageUtils::encodePng(portrait);
    QVERIFY(!portraitPng.isEmpty());

    QJsonObject first;
    first.insert(QStringLiteral("name"), QStringLiteral("Imported Emhyr"));
    first.insert(QStringLiteral("notes"), QStringLiteral("Restored from export"));
    first.insert(QStringLiteral("favorite"), true);
    first.insert(QStringLiteral("base64"), QString::fromLatin1(dna.toBase64()));
    first.insert(QStringLiteral("rulerDesigner"), ruler);
    first.insert(QStringLiteral("createdAt"), 1234567890000.0);
    first.insert(QStringLiteral("updatedAt"), 1234567890123.0);
    first.insert(QStringLiteral("portraitPng"),
                 QString::fromLatin1(portraitPng.toBase64()));

    QJsonObject second;
    second.insert(QStringLiteral("name"), QStringLiteral("Imported metadata"));
    second.insert(QStringLiteral("base64"), QString::fromLatin1(dna.toBase64()));
    second.insert(QStringLiteral("rulerMeta"), metadata);
    second.insert(QStringLiteral("createdAt"), 1234567891000.0);
    second.insert(QStringLiteral("updatedAt"), 1234567891123.0);

    QJsonArray entries;
    entries.append(first);
    entries.append(second);

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("ck3-dna-manager-qt"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("entries"), entries);

    const QString path = writeJsonFile(directory, QStringLiteral("export.json"),
                                       QJsonDocument(root));
    QVERIFY(!path.isEmpty());

    const LegacyImportResult result = LegacyImporter::importFile(
        path, database, m_catalog, m_legacyCatalog);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.imported, 2);
    QCOMPARE(result.portraits, 1);
    QCOMPARE(result.skipped, 0);

    EntryRecord emhyr;
    EntryRecord metadataOnly;
    for (const EntryRecord &summary : database.entries(&error)) {
        EntryRecord full;
        QVERIFY2(database.loadEntry(summary.id, &full, &error), qPrintable(error));
        if (full.name == QStringLiteral("Imported Emhyr"))
            emhyr = full;
        else if (full.name == QStringLiteral("Imported metadata"))
            metadataOnly = full;
    }

    QVERIFY(!emhyr.id.isEmpty());
    QCOMPARE(emhyr.notes, QStringLiteral("Restored from export"));
    QVERIFY(emhyr.favorite);
    QCOMPARE(emhyr.createdAt, 1234567890000LL);
    QCOMPARE(emhyr.updatedAt, 1234567890123LL);
    QVERIFY(!emhyr.portrait.isEmpty());
    QVERIFY(!emhyr.thumbnail.isEmpty());
    QVERIFY(!emhyr.rulerMeta.isEmpty());

    DnaParseResult restored = DnaCodec::fromBytes(
        emhyr.dna, Base64Hint::Modern, m_catalog, m_legacyCatalog);
    QVERIFY2(restored.ok, qPrintable(restored.error));
    DnaCodec::applyRulerMetadata(restored.data, emhyr.rulerMeta);
    QVERIFY(DnaCodec::toRuler(restored.data, m_catalog)
                .contains(QStringLiteral("mod_gene_x")));

    QVERIFY(!metadataOnly.id.isEmpty());
    QCOMPARE(metadataOnly.notes, QString());
    QVERIFY(!metadataOnly.favorite);
    QVERIFY(!metadataOnly.rulerMeta.isEmpty());

    database.close();
}

void TestCore::importRejectsUnsupportedFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    Database database;
    QString error;
    QVERIFY2(database.open(directory.filePath(QStringLiteral("dna.db")), &error),
             qPrintable(error));

    QJsonObject unsupported;
    unsupported.insert(QStringLiteral("hello"), 1);
    QString path = writeJsonFile(directory, QStringLiteral("unsupported.json"),
                                 QJsonDocument(unsupported));
    LegacyImportResult result = LegacyImporter::importFile(
        path, database, m_catalog, m_legacyCatalog);
    QVERIFY(!result.ok);
    QVERIFY(result.unsupportedFormat);
    QCOMPARE(result.imported, 0);

    QJsonObject emptyRoot;
    emptyRoot.insert(QStringLiteral("format"), QStringLiteral("ck3-dna-manager-qt"));
    emptyRoot.insert(QStringLiteral("version"), 1);
    emptyRoot.insert(QStringLiteral("entries"), QJsonArray());
    path = writeJsonFile(directory, QStringLiteral("empty.json"),
                         QJsonDocument(emptyRoot));
    result = LegacyImporter::importFile(path, database, m_catalog, m_legacyCatalog);
    QVERIFY(!result.ok);
    QVERIFY(result.emptyFile);
    QCOMPARE(result.imported, 0);

    QJsonObject futureRoot = emptyRoot;
    futureRoot.insert(QStringLiteral("version"), 2);
    path = writeJsonFile(directory, QStringLiteral("future.json"),
                         QJsonDocument(futureRoot));
    result = LegacyImporter::importFile(path, database, m_catalog, m_legacyCatalog);
    QVERIFY(!result.ok);
    QVERIFY(result.unsupportedVersion);
    QCOMPARE(result.imported, 0);

    database.close();
}

void TestCore::importPreservesLegacyFormat()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    Database database;
    QString error;
    QVERIFY2(database.open(directory.filePath(QStringLiteral("dna.db")), &error),
             qPrintable(error));

    QJsonObject legacy;
    legacy.insert(QStringLiteral("id"), 1);
    legacy.insert(QStringLiteral("name"), QStringLiteral("Legacy import"));
    legacy.insert(QStringLiteral("dna"),
                  QString::fromLatin1(legacyPayload().toBase64()));
    QJsonArray items;
    items.append(legacy);

    const QString path = writeJsonFile(directory, QStringLiteral("legacy.json"),
                                       QJsonDocument(items));
    const LegacyImportResult result = LegacyImporter::importFile(
        path, database, m_catalog, m_legacyCatalog);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.imported, 1);
    QCOMPARE(result.portraits, 0);
    QCOMPARE(result.skipped, 0);

    EntryRecord loaded;
    const QVector<EntryRecord> entries = database.entries(&error);
    QCOMPARE(entries.size(), 1);
    QVERIFY2(database.loadEntry(entries.first().id, &loaded, &error),
             qPrintable(error));
    QCOMPARE(loaded.name, QStringLiteral("Legacy import"));
    QCOMPARE(loaded.dna.size(), kModernGeneCount * kBytesPerGene);

    database.close();
}

void TestCore::translationsAreComplete()
{
    I18n i18n;
    QString error;
    QVERIFY2(i18n.load(QStringLiteral(":/i18n/translations.json"), &error),
             qPrintable(error));

    const QStringList expected = {
        QStringLiteral("en"), QStringLiteral("de"), QStringLiteral("fr"),
        QStringLiteral("zh"), QStringLiteral("ru"), QStringLiteral("es"),
        QStringLiteral("pt"), QStringLiteral("pl"), QStringLiteral("ja"),
        QStringLiteral("ko"),
    };
    QCOMPARE(i18n.availableLanguages(), expected);

    const QStringList english = i18n.keysForLanguage(QStringLiteral("en"));
    QVERIFY(english.size() >= 150);
    QVERIFY(english.contains(QStringLiteral("app.title")));
    QVERIFY(english.contains(QStringLiteral("genes.category.face")));

    for (const QString &code : expected) {
        QVERIFY2(i18n.hasLanguage(code), qPrintable(code));
        QVERIFY(!i18n.languageName(code).isEmpty());
        QCOMPARE(i18n.keysForLanguage(code), english);
    }

    i18n.setLanguage(QStringLiteral("de"));
    QCOMPARE(i18n.currentLanguage(), QStringLiteral("de"));
    QVERIFY(i18n.t(QStringLiteral("menu.file")) != QStringLiteral("menu.file"));
    QVERIFY(i18n.t(QStringLiteral("genes.category.face"))
            != QStringLiteral("genes.category.face"));

    const QString filtered = i18n.t(QStringLiteral("library.filteredCount"),
                                    QStringLiteral("3"), QStringLiteral("10"));
    QVERIFY(filtered.contains(QStringLiteral("3")));
    QVERIFY(filtered.contains(QStringLiteral("10")));
}

void TestCore::translationFallback()
{
    I18n i18n;
    QString error;
    QVERIFY2(i18n.load(QStringLiteral(":/i18n/translations.json"), &error),
             qPrintable(error));

    QCOMPARE(i18n.t(QStringLiteral("this.key.does.not.exist")),
             QStringLiteral("this.key.does.not.exist"));

    // German lacks no keys, but the manager must still fall back to English
    // when a language is requested that is not present.
    QVERIFY(!i18n.hasLanguage(QStringLiteral("xx")));
    i18n.setLanguage(QStringLiteral("xx"));
    QCOMPARE(i18n.currentLanguage(), QStringLiteral("en"));
}

void TestCore::thumbnailIsCropped()
{
    QImage image(200, 300, QImage::Format_ARGB32);
    image.fill(Qt::darkRed);

    const QByteArray thumbnail = ImageUtils::makeThumbnail(image, QSize(96, 120));
    QVERIFY(!thumbnail.isEmpty());

    const QImage decoded = ImageUtils::imageFromBytes(thumbnail);
    QVERIFY(!decoded.isNull());
    QCOMPARE(decoded.size(), QSize(96, 120));

    QVERIFY(ImageUtils::makeThumbnail(QImage()).isEmpty());
    QVERIFY(ImageUtils::imageFromBytes(QByteArray()).isNull());
}

void TestCore::imageFormatSupport()
{
    QVERIFY(ImageUtils::supportedImageSuffixes().contains(QStringLiteral("png")));
    QVERIFY(ImageUtils::supportedImageSuffixes().contains(QStringLiteral("webp")));

    QVERIFY(ImageUtils::isSupportedImagePath(QStringLiteral("portrait.png")));
    QVERIFY(ImageUtils::isSupportedImagePath(QStringLiteral("portrait.WEBP")));
    QVERIFY(ImageUtils::isSupportedImagePath(QStringLiteral("C:/pictures/Portrait.JpEg")));
    QVERIFY(!ImageUtils::isSupportedImagePath(QStringLiteral("portrait.txt")));
    QVERIFY(!ImageUtils::isSupportedImagePath(QStringLiteral("portrait")));
}

void TestCore::webPImageDecodes()
{
    // A 4x4 lossless WebP produced with libwebp.
    const QByteArray webp = QByteArray::fromBase64(
        QByteArrayLiteral("UklGRh4AAABXRUJQVlA4TBEAAAAvA8AAAAfQ22JXqf+BiOh/AAA="));

    const QImage image = ImageUtils::imageFromBytes(webp);
    if (image.isNull())
        QSKIP("No WebP decoder is available in this environment.");
    QCOMPARE(image.size(), QSize(4, 4));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("portrait.webp"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(webp), qint64(webp.size()));
    file.close();

    QString error;
    const QImage fromFile = ImageUtils::loadImage(path, &error);
    QVERIFY2(!fromFile.isNull(), qPrintable(error));
    QCOMPARE(fromFile.size(), QSize(4, 4));
}

QTEST_GUILESS_MAIN(TestCore)

#include "test_core.moc"

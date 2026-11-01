// Development helper: renders the real main window offscreen with a seeded
// demo library so the layout can be reviewed without touching user data.
//
// Build with -DBUILD_SCREENSHOTS=ON, then:
//   ck3_screenshots --out screenshots/overview.png --tab overview
//   ck3_screenshots --out screenshots/genes.png --tab genes --language de

#include "core/Database.h"
#include "core/DnaCodec.h"
#include "core/DnaData.h"
#include "core/GeneCatalog.h"
#include "core/I18n.h"
#include "core/ImageUtils.h"
#include "ui/AboutDialog.h"
#include "ui/AddEntryDialog.h"
#include "ui/AppStyle.h"
#include "ui/EntryListDelegate.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QLinearGradient>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRadialGradient>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>

#include <array>
#include <cmath>
#include <memory>

namespace {

struct PortraitStyle
{
    QColor background;
    QColor skin;
    QColor hair;
    QColor cloak;
    QColor tunic;
    QColor eyes;
    bool beard = false;
    bool longHair = false;
};

QImage renderPortrait(const PortraitStyle &style)
{
    QImage image(600, 800, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient background(0, 0, 0, image.height());
    background.setColorAt(0.0, style.background.lighter(135));
    background.setColorAt(0.55, style.background);
    background.setColorAt(1.0, style.background.darker(220));
    painter.fillRect(image.rect(), background);

    QRadialGradient glow(300, 300, 330);
    glow.setColorAt(0.0, QColor(255, 226, 160, 45));
    glow.setColorAt(1.0, QColor(255, 226, 160, 0));
    painter.fillRect(image.rect(), glow);

    painter.setPen(Qt::NoPen);

    // Shoulders and cloak.
    QPainterPath cloak;
    cloak.moveTo(18, 800);
    cloak.cubicTo(55, 660, 130, 575, 215, 535);
    cloak.cubicTo(250, 520, 270, 505, 278, 480);
    cloak.lineTo(322, 480);
    cloak.cubicTo(330, 505, 350, 520, 385, 535);
    cloak.cubicTo(470, 575, 545, 660, 582, 800);
    cloak.closeSubpath();
    painter.fillPath(cloak, style.cloak);

    // Inner tunic and collar.
    QPainterPath tunic;
    tunic.moveTo(150, 800);
    tunic.cubicTo(175, 690, 225, 605, 262, 560);
    tunic.lineTo(338, 560);
    tunic.cubicTo(375, 605, 425, 690, 450, 800);
    tunic.closeSubpath();
    painter.fillPath(tunic, style.tunic);

    QPainterPath collar;
    collar.moveTo(248, 548);
    collar.lineTo(300, 620);
    collar.lineTo(352, 548);
    collar.lineTo(338, 534);
    collar.lineTo(300, 566);
    collar.lineTo(262, 534);
    collar.closeSubpath();
    painter.fillPath(collar, style.cloak.lighter(118));

    // Long hair behind the head.
    if (style.longHair) {
        QPainterPath backHair;
        backHair.moveTo(196, 250);
        backHair.cubicTo(160, 330, 168, 470, 188, 610);
        backHair.cubicTo(224, 646, 376, 646, 412, 610);
        backHair.cubicTo(432, 470, 440, 330, 404, 250);
        backHair.closeSubpath();
        painter.fillPath(backHair, style.hair.darker(125));
    }

    // Neck.
    painter.setBrush(style.skin.darker(112));
    painter.drawRoundedRect(QRectF(258, 430, 84, 120), 30, 30);

    // Ears.
    painter.setBrush(style.skin.darker(104));
    painter.drawEllipse(QRectF(184, 330, 34, 72));
    painter.drawEllipse(QRectF(382, 330, 34, 72));

    // Head.
    QRadialGradient face(280, 300, 210);
    face.setColorAt(0.0, style.skin.lighter(112));
    face.setColorAt(0.62, style.skin);
    face.setColorAt(1.0, style.skin.darker(120));
    painter.setBrush(face);
    painter.drawEllipse(QRectF(192, 200, 216, 310));

    // Hair cap and fringe.
    QPainterPath hair;
    hair.moveTo(196, 330);
    hair.cubicTo(184, 232, 232, 178, 300, 178);
    hair.cubicTo(368, 178, 416, 232, 404, 330);
    hair.cubicTo(396, 272, 372, 242, 340, 236);
    hair.cubicTo(314, 258, 270, 264, 232, 252);
    hair.cubicTo(212, 274, 202, 298, 196, 330);
    hair.closeSubpath();
    painter.fillPath(hair, style.hair);

    if (style.longHair) {
        QPainterPath sideHair;
        sideHair.moveTo(200, 300);
        sideHair.cubicTo(180, 380, 184, 480, 196, 560);
        sideHair.cubicTo(212, 574, 230, 578, 242, 574);
        sideHair.cubicTo(222, 490, 220, 390, 230, 316);
        sideHair.closeSubpath();
        painter.fillPath(sideHair, style.hair);
        QPainterPath other = sideHair;
        painter.save();
        painter.translate(600, 0);
        painter.scale(-1, 1);
        painter.fillPath(other, style.hair);
        painter.restore();
    }

    // Brows.
    QPen brow(style.hair.darker(135), 9, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(brow);
    painter.drawArc(QRectF(238, 328, 58, 30), 20 * 16, 140 * 16);
    painter.drawArc(QRectF(304, 328, 58, 30), 20 * 16, 140 * 16);

    // Eyes.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(238, 231, 214));
    painter.drawEllipse(QRectF(246, 354, 52, 25));
    painter.drawEllipse(QRectF(302, 354, 52, 25));
    painter.setBrush(style.eyes);
    painter.drawEllipse(QRectF(264, 358, 17, 17));
    painter.drawEllipse(QRectF(320, 358, 17, 17));
    painter.setBrush(QColor(28, 24, 19));
    painter.drawEllipse(QRectF(269, 362, 8, 8));
    painter.drawEllipse(QRectF(325, 362, 8, 8));

    // Nose and mouth.
    painter.setPen(QPen(style.skin.darker(140), 5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(300, 382), QPointF(294, 426));
    painter.drawArc(QRectF(286, 414, 28, 20), 200 * 16, 140 * 16);
    painter.setPen(QPen(QColor(126, 72, 66), 6, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(QRectF(270, 438, 60, 30), 200 * 16, 140 * 16);

    if (style.beard) {
        QPainterPath beard;
        beard.moveTo(204, 366);
        beard.cubicTo(210, 470, 252, 528, 300, 536);
        beard.cubicTo(348, 528, 390, 470, 396, 366);
        beard.cubicTo(376, 438, 348, 462, 300, 462);
        beard.cubicTo(252, 462, 224, 438, 204, 366);
        beard.closeSubpath();
        QColor beardColor = style.hair;
        beardColor.setAlpha(225);
        painter.fillPath(beard, beardColor);
    }

    // Warm rim light and vignette.
    QRadialGradient rim(300, 330, 340);
    rim.setColorAt(0.0, QColor(255, 220, 150, 0));
    rim.setColorAt(0.72, QColor(255, 220, 150, 0));
    rim.setColorAt(1.0, QColor(255, 214, 140, 28));
    painter.fillRect(image.rect(), rim);

    QRadialGradient vignette(300, 400, 520);
    vignette.setColorAt(0.0, QColor(0, 0, 0, 0));
    vignette.setColorAt(1.0, QColor(0, 0, 0, 150));
    painter.fillRect(image.rect(), vignette);

    return image;
}

DnaData sampleDna(const GeneCatalog &catalog, int seed, int extraCount)
{
    DnaData data;
    for (int slot = 0; slot < catalog.base64GeneCount(); ++slot) {
        const GeneDefinition *gene = catalog.byBase64Index(slot);
        if (!gene)
            continue;

        if (gene->isColor()) {
            const int shift = (seed * 29 + slot * 7) % 70;
            data.setColor(gene->key,
                          { static_cast<quint8>(95 + shift),
                            static_cast<quint8>(72 + shift),
                            static_cast<quint8>(52 + shift),
                            static_cast<quint8>(255) });
            continue;
        }

        const int optionCount = qMax(1, static_cast<int>(gene->options.size()));
        GenePair pair;
        pair.first.variant = (slot * 5 + seed * 3) % optionCount;
        pair.first.intensity = 120 + (slot * 17 + seed * 11) % 130;
        pair.second.variant = (slot * 3 + seed * 7 + 2) % optionCount;
        pair.second.intensity = 110 + (slot * 13 + seed * 19) % 140;
        data.setGene(gene->key, pair);
    }

    for (int i = 0; i < extraCount; ++i) {
        ExtraGene extra;
        extra.key = QStringLiteral("mod_gene_%1").arg(i + 1, 2, 10, QLatin1Char('0'));
        extra.bytes = { static_cast<quint8>(i * 7 + 3), 180,
                        static_cast<quint8>(i * 11 + 9), 96 };
        data.extras.append(extra);
    }

    return data;
}

struct SampleEntry
{
    const char *name;
    const char *notes;
    bool favorite;
    int seed;
    int extras;
    PortraitStyle portrait;
};

QVector<SampleEntry> sampleEntries()
{
    return {
        { "Countess Adela of Vermandois",
          "Heir to the northern march.\nAuburn hair, hazel eyes, a knowing smile.",
          true, 3, 0,
          { QColor(0x35, 0x2c, 0x22), QColor(0xe0, 0xb0, 0x86),
            QColor(0x6d, 0x3a, 0x1d), QColor(0x3f, 0x4d, 0x38),
            QColor(0x76, 0x38, 0x32), QColor(0x6d, 0x76, 0x38), false, true } },
        { "Duke Bertram the Bold",
          "Grizzled veteran of the Alpine campaigns.",
          true, 7, 0,
          { QColor(0x2c, 0x30, 0x37), QColor(0xd3, 0x9f, 0x77),
            QColor(0x8d, 0x86, 0x7b), QColor(0x2f, 0x3a, 0x50),
            QColor(0x63, 0x2e, 0x2b), QColor(0x4d, 0x66, 0x7a), true, false } },
        { "Elena of Valois",
          "Court beauty of the Île-de-France.",
          false, 12, 0,
          { QColor(0x3a, 0x2a, 0x2f), QColor(0xe7, 0xc0, 0x9a),
            QColor(0x3c, 0x28, 0x22), QColor(0x52, 0x33, 0x52),
            QColor(0x7f, 0x5a, 0x72), QColor(0x55, 0x78, 0x6b), false, true } },
        { "Modded Heir — 13 extra genes",
          "Imported from a total-conversion mod.\nExtra slots are preserved exactly.",
          false, 17, 13,
          { QColor(0x26, 0x32, 0x2c), QColor(0xc9, 0x96, 0x6f),
            QColor(0x24, 0x1c, 0x18), QColor(0x2e, 0x3a, 0x2c),
            QColor(0x5d, 0x4b, 0x2e), QColor(0x70, 0x5e, 0x34), true, false } },
    };
}

void seedLibrary(const GeneCatalog &catalog, const QString &databasePath)
{
    Database database;
    QString error;
    if (!database.open(databasePath, &error)) {
        qWarning("Cannot open demo database: %s", qPrintable(error));
        return;
    }

    if (!database.entries().isEmpty())
        return;

    const auto samples = sampleEntries();
    qint64 created = QDateTime::currentMSecsSinceEpoch() - samples.size() * 86'400'000;

    for (const SampleEntry &sample : samples) {
        EntryRecord entry;
        entry.name = QString::fromUtf8(sample.name);
        entry.notes = QString::fromUtf8(sample.notes);
        entry.favorite = sample.favorite;
        entry.dna = DnaCodec::toBytes(sampleDna(catalog, sample.seed, sample.extras), catalog);
        const QImage portrait = renderPortrait(sample.portrait);
        entry.portrait = ImageUtils::encodePng(portrait);
        entry.thumbnail = ImageUtils::makeThumbnail(portrait);
        entry.createdAt = created;
        created += 86'400'000;

        if (!database.insert(entry, &error))
            qWarning("Cannot seed demo entry: %s", qPrintable(error));
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CK3 DNA Manager Qt Screenshots"));
    QCoreApplication::setApplicationName(QStringLiteral("CK3 DNA Manager Qt Screenshots"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    applyAppStyle(application);
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    QFile themeFile(QStringLiteral(":/theme.qss"));
    if (themeFile.open(QIODevice::ReadOnly))
        application.setStyleSheet(QString::fromUtf8(themeFile.readAll()));
    QStandardPaths::setTestModeEnabled(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Render CK3 DNA Manager screenshots."));
    parser.addHelpOption();
    const QCommandLineOption outputOption(QStringLiteral("out"),
                                          QStringLiteral("PNG output path."),
                                          QStringLiteral("path"));
    const QCommandLineOption tabOption(QStringLiteral("tab"),
                                       QStringLiteral("Tab to show: overview or genes."),
                                       QStringLiteral("name"),
                                       QStringLiteral("overview"));
    const QCommandLineOption languageOption(QStringLiteral("language"),
                                            QStringLiteral("UI language code."),
                                            QStringLiteral("code"),
                                            QStringLiteral("en"));
    const QCommandLineOption sizeOption(QStringLiteral("size"),
                                        QStringLiteral("Window size, e.g. 1080x700."),
                                        QStringLiteral("WxH"),
                                        QStringLiteral("1320x860"));
    const QCommandLineOption entryOption(QStringLiteral("entry"),
                                         QStringLiteral("Name of the demo entry to select."),
                                         QStringLiteral("name"));
    const QCommandLineOption emptyOption(QStringLiteral("empty"),
                                         QStringLiteral("Show the empty library instead of demo data."));
    const QCommandLineOption dialogOption(QStringLiteral("dialog"),
                                          QStringLiteral("Capture a dialog instead: add or about."),
                                          QStringLiteral("name"));
    parser.addOption(outputOption);
    parser.addOption(tabOption);
    parser.addOption(languageOption);
    parser.addOption(sizeOption);
    parser.addOption(entryOption);
    parser.addOption(emptyOption);
    parser.addOption(dialogOption);
    parser.process(application);

    const QString output = parser.value(outputOption);
    if (output.isEmpty()) {
        qWarning("--out is required");
        return 2;
    }

    QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    // MainWindow keeps a single folder below the organization directory.
    if (!directory.isEmpty())
        directory = QFileInfo(directory).dir().absolutePath();
#endif
    if (directory.isEmpty())
        directory = QDir::home().filePath(QStringLiteral(".ck3-dna-manager-screenshots"));
    QDir().mkpath(directory);
    const QString databasePath = QDir(directory).filePath(QStringLiteral("dna.db"));

    GeneCatalog catalog;
    QString error;
    if (!catalog.load(QStringLiteral(":/data/genes.json"), &error)) {
        qWarning("Cannot load gene catalog: %s", qPrintable(error));
        return 1;
    }

    const QString absoluteOutput = QFileInfo(output).absoluteFilePath();
    QDir().mkpath(QFileInfo(absoluteOutput).absolutePath());

    const QString dialog = parser.value(dialogOption);
    if (!dialog.isEmpty()) {
        I18n i18n;
        if (!i18n.load(QStringLiteral(":/i18n/translations.json"), &error)) {
            qWarning("Cannot load translations: %s", qPrintable(error));
            return 1;
        }
        i18n.setLanguage(parser.value(languageOption));

        std::unique_ptr<QDialog> capture;
        if (dialog == QStringLiteral("add")) {
            GeneCatalog legacy;
            if (!legacy.load(QStringLiteral(":/data/legacy_genes.json"), &error)) {
                qWarning("Cannot load legacy catalog: %s", qPrintable(error));
                return 1;
            }
            auto *add = new AddEntryDialog(catalog, legacy, i18n);
            add->resize(960, 620);
            capture.reset(add);
        } else if (dialog == QStringLiteral("about")) {
            capture.reset(new AboutDialog(i18n));
        } else {
            qWarning("Unknown dialog: %s", qPrintable(dialog));
            return 2;
        }

        capture->show();
        QTimer::singleShot(900, &application, [dialog = capture.get(), absoluteOutput]() {
            if (dialog->grab().save(absoluteOutput, "PNG"))
                qInfo("Saved %s", qPrintable(absoluteOutput));
            QCoreApplication::quit();
        });
        return application.exec();
    }

    seedLibrary(catalog, databasePath);
    if (parser.isSet(emptyOption)) {
        QFile::remove(databasePath);
        QFile::remove(databasePath + QStringLiteral("-wal"));
        QFile::remove(databasePath + QStringLiteral("-shm"));
    }

    QSettings settings;
    settings.setValue(QStringLiteral("language"), parser.value(languageOption));

    MainWindow window;
    if (!window.initialize(&error)) {
        qWarning("Cannot start the main window: %s", qPrintable(error));
        return 1;
    }

    window.resize(1320, 860);
    const QStringList size = parser.value(sizeOption).split(QLatin1Char('x'));
    if (size.size() == 2) {
        const int width = size.at(0).toInt();
        const int height = size.at(1).toInt();
        if (width > 0 && height > 0)
            window.resize(width, height);
    }
    window.show();
    window.refreshAll();

    const QString wantedEntry = parser.value(entryOption);
    if (!wantedEntry.isEmpty()) {
        if (auto *list = window.findChild<QListWidget *>(QStringLiteral("entryList"))) {
            for (int i = 0; i < list->count(); ++i) {
                QListWidgetItem *item = list->item(i);
                if (item->data(EntryRoles::Name).toString() == wantedEntry) {
                    list->setCurrentItem(item);
                    break;
                }
            }
        }
    }

    const QString tab = parser.value(tabOption);
    if (tab == QStringLiteral("genes")) {
        if (auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("mainTabs")))
            tabs->setCurrentIndex(1);
    }

    QTimer::singleShot(900, &application, [&window, absoluteOutput]() {
        const QPixmap pixmap = window.grab();
        if (pixmap.save(absoluteOutput, "PNG"))
            qInfo("Saved %s", qPrintable(absoluteOutput));
        else
            qWarning("Could not save %s", qPrintable(absoluteOutput));
        QCoreApplication::quit();
    });

    return application.exec();
}

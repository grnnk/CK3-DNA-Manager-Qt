#include "ui/AddEntryDialog.h"

#include "core/GeneCatalog.h"
#include "core/I18n.h"
#include "core/ImageUtils.h"
#include "ui/FlowLayout.h"
#include "ui/PortraitWidget.h"

#include <QComboBox>
#include <QClipboard>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

AddEntryDialog::AddEntryDialog(const GeneCatalog &catalog,
                               const GeneCatalog &legacyCatalog,
                               I18n &i18n,
                               QWidget *parent,
                               const QImage &initialImage)
    : QDialog(parent)
    , m_catalog(catalog)
    , m_legacyCatalog(legacyCatalog)
    , m_i18n(i18n)
{
    buildUi();
    retranslateUi();
    if (!initialImage.isNull()) {
        m_image = initialImage;
        m_portrait->setImage(initialImage);
        updateCreateButton();
    }
    setMinimumSize(900, 580);
    resize(960, 620);
}

void AddEntryDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(14);

    auto *body = new QHBoxLayout();
    body->setSpacing(20);

    auto *left = new QVBoxLayout();
    left->setSpacing(8);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setObjectName(QStringLiteral("fieldLabel"));
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setClearButtonEnabled(true);
    left->addWidget(m_nameLabel);
    left->addWidget(m_nameEdit);

    m_dnaLabel = new QLabel(this);
    m_dnaLabel->setObjectName(QStringLiteral("fieldLabel"));
    left->addWidget(m_dnaLabel);

    m_dnaEdit = new QPlainTextEdit(this);
    m_dnaEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_dnaEdit->setTabChangesFocus(true);
    m_dnaEdit->setMinimumHeight(300);
    left->addWidget(m_dnaEdit, 1);

    auto *formatRow = new QHBoxLayout();
    formatRow->setSpacing(10);
    m_formatLabel = new QLabel(this);
    m_formatLabel->setObjectName(QStringLiteral("fieldLabel"));
    m_formatCombo = new QComboBox(this);
    m_formatCombo->setMinimumWidth(190);
    formatRow->addWidget(m_formatLabel);
    formatRow->addWidget(m_formatCombo);
    formatRow->addStretch(1);
    left->addLayout(formatRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_statusLabel->setMinimumHeight(40);
    left->addWidget(m_statusLabel);

    body->addLayout(left, 1);

    auto *right = new QVBoxLayout();
    right->setSpacing(8);
    m_portraitLabel = new QLabel(this);
    m_portraitLabel->setObjectName(QStringLiteral("fieldLabel"));
    right->addWidget(m_portraitLabel);

    m_portrait = new PortraitWidget(this);
    m_portrait->setMinimumSize(240, 300);
    m_portrait->setMaximumWidth(300);
    right->addWidget(m_portrait, 1);

    auto *imageRow = new FlowLayout(nullptr, 0, 8, 8);
    imageRow->setSpacing(8);
    m_chooseImageButton = new QPushButton(this);
    m_chooseImageButton->setObjectName(QStringLiteral("secondaryButton"));
    m_pasteImageButton = new QPushButton(this);
    m_pasteImageButton->setObjectName(QStringLiteral("secondaryButton"));
    m_removeImageButton = new QPushButton(this);
    m_removeImageButton->setObjectName(QStringLiteral("secondaryButton"));
    imageRow->addWidget(m_chooseImageButton);
    imageRow->addWidget(m_pasteImageButton);
    imageRow->addWidget(m_removeImageButton);
    right->addLayout(imageRow);

    body->addLayout(right);

    root->addLayout(body, 1);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    m_cancelButton = new QPushButton(this);
    m_cancelButton->setObjectName(QStringLiteral("secondaryButton"));
    m_createButton = new QPushButton(this);
    m_createButton->setObjectName(QStringLiteral("primaryButton"));
    m_createButton->setDefault(true);
    buttonRow->addWidget(m_cancelButton);
    buttonRow->addWidget(m_createButton);
    root->addLayout(buttonRow);

    m_validationTimer = new QTimer(this);
    m_validationTimer->setSingleShot(true);
    m_validationTimer->setInterval(220);

    connect(m_dnaEdit, &QPlainTextEdit::textChanged, this, [this] {
        m_validationTimer->start();
    });
    connect(m_validationTimer, &QTimer::timeout, this, &AddEntryDialog::validateInput);
    m_nameEdit->installEventFilter(this);
    m_dnaEdit->installEventFilter(this);
    connect(m_formatCombo, &QComboBox::currentIndexChanged, this, [this] {
        validateInput();
    });
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this] {
        updateCreateButton();
    });
    connect(m_chooseImageButton, &QPushButton::clicked, this, &AddEntryDialog::chooseImage);
    connect(m_pasteImageButton, &QPushButton::clicked, this, &AddEntryDialog::pasteImage);
    connect(m_removeImageButton, &QPushButton::clicked, this, &AddEntryDialog::removeImage);
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            &AddEntryDialog::updatePasteButton);
    connect(m_portrait, &PortraitWidget::chooseRequested, this, &AddEntryDialog::chooseImage);
    connect(m_portrait, &PortraitWidget::imageDropped, this, [this](const QImage &image) {
        m_image = image;
        m_portrait->setImage(image);
        updateCreateButton();
    });
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_createButton, &QPushButton::clicked, this, &AddEntryDialog::accept);
}

void AddEntryDialog::retranslateUi()
{
    setWindowTitle(m_i18n.t(QStringLiteral("add.title")));
    m_nameLabel->setText(m_i18n.t(QStringLiteral("add.name")));
    m_nameEdit->setPlaceholderText(m_i18n.t(QStringLiteral("add.namePlaceholder")));
    m_dnaLabel->setText(m_i18n.t(QStringLiteral("add.dna")));
    m_dnaEdit->setPlaceholderText(m_i18n.t(QStringLiteral("add.dnaPlaceholder")));
    m_formatLabel->setText(m_i18n.t(QStringLiteral("add.format")));
    m_portraitLabel->setText(m_i18n.t(QStringLiteral("add.portrait")));
    m_chooseImageButton->setText(m_i18n.t(QStringLiteral("add.chooseImage")));
    m_pasteImageButton->setText(m_i18n.t(QStringLiteral("action.pasteImage")));
    m_pasteImageButton->setToolTip(m_i18n.t(QStringLiteral("action.pasteImageHint")));
    m_removeImageButton->setText(m_i18n.t(QStringLiteral("add.removeImage")));
    m_createButton->setText(m_i18n.t(QStringLiteral("add.create")));
    m_cancelButton->setText(m_i18n.t(QStringLiteral("msg.cancel")));
    m_portrait->setPlaceholderText(m_i18n.t(QStringLiteral("add.dropImage")));

    const int previous = m_formatCombo->currentData().toInt();
    {
        QSignalBlocker blocker(m_formatCombo);
        m_formatCombo->clear();
        m_formatCombo->addItem(m_i18n.t(QStringLiteral("add.formatAuto")), 0);
        m_formatCombo->addItem(m_i18n.t(QStringLiteral("add.formatModern")), 1);
        m_formatCombo->addItem(m_i18n.t(QStringLiteral("add.formatLegacy")), 2);
        const int index = m_formatCombo->findData(previous);
        m_formatCombo->setCurrentIndex(index >= 0 ? index : 0);
    }

    if (m_dnaEdit->toPlainText().trimmed().isEmpty())
        setStatus(m_i18n.t(QStringLiteral("add.pasteHint")), QStringLiteral("Muted"));
    else
        validateInput();

    updateCreateButton();
}

Base64Hint AddEntryDialog::base64Hint() const
{
    switch (m_formatCombo->currentData().toInt()) {
    case 1:
        return Base64Hint::Modern;
    case 2:
        return Base64Hint::Legacy;
    default:
        return Base64Hint::Auto;
    }
}

void AddEntryDialog::validateInput()
{
    const QString text = m_dnaEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        m_parsedOk = false;
        m_parseResult = {};
        setStatus(m_i18n.t(QStringLiteral("add.pasteHint")), QStringLiteral("Muted"));
        updateCreateButton();
        return;
    }

    m_parseResult = DnaCodec::parse(text, base64Hint(), m_catalog, m_legacyCatalog);
    m_parsedOk = m_parseResult.ok;
    if (!m_parseResult.ok) {
        setStatus(m_parseResult.error, QStringLiteral("Error"));
        updateCreateButton();
        return;
    }

    QString message;
    switch (m_parseResult.format) {
    case DnaFormat::LegacyBase64:
        message = m_i18n.t(QStringLiteral("add.detectedLegacy"))
                      .arg(m_parseResult.geneCount);
        break;
    case DnaFormat::RulerDesigner:
        message = m_i18n.t(QStringLiteral("add.detectedRuler"))
                      .arg(m_parseResult.geneCount);
        break;
    case DnaFormat::ModernBase64:
    default:
        message = m_i18n.t(QStringLiteral("add.detectedModern"))
                      .arg(m_parseResult.geneCount);
        break;
    }

    int modded = 0;
    int rulerOnly = 0;
    QStringList nonVanillaKeys;
    for (const ExtraGene &extra : m_parseResult.data.extras) {
        if (extra.kind == ExtraGeneKind::RulerOnly) {
            ++rulerOnly;
        } else {
            ++modded;
        }
        nonVanillaKeys.append(extra.key);
    }
    if (modded > 0)
        message += m_i18n.t(QStringLiteral("add.moddedSuffix")).arg(modded);
    if (rulerOnly > 0)
        message += m_i18n.t(QStringLiteral("add.rulerOnlySuffix")).arg(rulerOnly);

    QStringList warnings = m_parseResult.warnings;
    if (!warnings.isEmpty()) {
        message += QStringLiteral(" ") + m_i18n.t(QStringLiteral("add.warnings"))
                                             .arg(warnings.size());
        setStatus(message, QStringLiteral("Warning"));
    } else {
        setStatus(message, QStringLiteral("Ok"));
    }

    QStringList tooltip = warnings;
    if (!nonVanillaKeys.isEmpty()) {
        if (!tooltip.isEmpty())
            tooltip.append(QString());
        tooltip.append(nonVanillaKeys);
    }
    m_statusLabel->setToolTip(tooltip.mid(0, 12).join(QLatin1Char('\n')));
    updateCreateButton();
}

void AddEntryDialog::setStatus(const QString &text, const QString &state)
{
    m_statusLabel->setText(text);
    m_statusLabel->setObjectName(QStringLiteral("status") + state);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void AddEntryDialog::updateCreateButton()
{
    const bool hasDna = !m_dnaEdit->toPlainText().trimmed().isEmpty();
    m_createButton->setEnabled(m_parsedOk && hasDna);
    m_removeImageButton->setEnabled(!m_image.isNull());
    updatePasteButton();
}

void AddEntryDialog::updatePasteButton()
{
    if (m_pasteImageButton)
        m_pasteImageButton->setEnabled(ImageUtils::hasClipboardImage());
}

void AddEntryDialog::chooseImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, m_i18n.t(QStringLiteral("add.chooseImage")), QString(),
        m_i18n.t(QStringLiteral("filter.images")));
    if (path.isEmpty())
        return;

    QString error;
    const QImage image = ImageUtils::loadImage(path, &error);
    if (image.isNull()) {
        QMessageBox box(QMessageBox::Critical,
                        m_i18n.t(QStringLiteral("msg.error")),
                        m_i18n.t(QStringLiteral("msg.imageError")).arg(error),
                        QMessageBox::NoButton, this);
        box.addButton(m_i18n.t(QStringLiteral("msg.ok")), QMessageBox::AcceptRole);
        box.exec();
        return;
    }
    m_image = image;
    m_portrait->setImage(image);
    updateCreateButton();
}

void AddEntryDialog::removeImage()
{
    m_image = QImage();
    m_portrait->clearImage();
    updateCreateButton();
}

void AddEntryDialog::pasteImage()
{
    const QImage image = ImageUtils::imageFromClipboard();
    if (image.isNull())
        return;
    m_image = image;
    m_portrait->setImage(image);
    updateCreateButton();
}

void AddEntryDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Paste) && ImageUtils::hasClipboardImage()) {
        pasteImage();
        return;
    }
    QDialog::keyPressEvent(event);
}

bool AddEntryDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress
        && (watched == m_nameEdit || watched == m_dnaEdit)) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->matches(QKeySequence::Paste)
            && ImageUtils::hasClipboardImage()
            && !ImageUtils::hasClipboardText()) {
            pasteImage();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void AddEntryDialog::accept()
{
    validateInput();
    if (!m_parsedOk) {
        m_dnaEdit->setFocus();
        return;
    }

    m_record = EntryRecord();
    m_record.name = m_nameEdit->text().trimmed();
    if (m_record.name.isEmpty())
        m_record.name = m_i18n.t(QStringLiteral("add.nameFallback"));
    m_record.dna = DnaCodec::toBytes(m_parseResult.data, m_catalog);
    m_record.rulerMeta = DnaCodec::serializeRulerMetadata(m_parseResult.data);
    if (!m_image.isNull()) {
        m_record.portrait = ImageUtils::encodePng(m_image);
        m_record.thumbnail = ImageUtils::makeThumbnail(m_image);
    }
    m_record.createdAt = QDateTime::currentMSecsSinceEpoch();

    QDialog::accept();
}

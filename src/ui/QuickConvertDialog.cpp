#include "ui/QuickConvertDialog.h"

#include "core/GeneCatalog.h"
#include "core/I18n.h"

#include <QClipboard>
#include <QComboBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

QuickConvertDialog::QuickConvertDialog(const GeneCatalog &catalog,
                                       const GeneCatalog &legacyCatalog,
                                       I18n &i18n,
                                       QWidget *parent)
    : QDialog(parent)
    , m_catalog(catalog)
    , m_legacyCatalog(legacyCatalog)
    , m_i18n(i18n)
{
    buildUi();
    retranslateUi();
    setMinimumSize(860, 580);
    resize(940, 640);
}

void QuickConvertDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(14);

    auto *body = new QHBoxLayout();
    body->setSpacing(20);

    auto *left = new QVBoxLayout();
    left->setSpacing(8);

    m_inputLabel = new QLabel(this);
    m_inputLabel->setObjectName(QStringLiteral("fieldLabel"));
    left->addWidget(m_inputLabel);

    m_inputEdit = new QPlainTextEdit(this);
    m_inputEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_inputEdit->setTabChangesFocus(true);
    m_inputEdit->setMinimumHeight(250);
    left->addWidget(m_inputEdit, 1);

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

    auto *right = new QVBoxLayout();
    right->setSpacing(8);

    m_outputLabel = new QLabel(this);
    m_outputLabel->setObjectName(QStringLiteral("fieldLabel"));
    right->addWidget(m_outputLabel);

    auto *base64Row = new QHBoxLayout();
    base64Row->setSpacing(8);
    m_base64Label = new QLabel(this);
    m_base64Label->setObjectName(QStringLiteral("fieldLabel"));
    m_copyBase64Button = new QPushButton(this);
    m_copyBase64Button->setObjectName(QStringLiteral("secondaryButton"));
    m_copyBase64Button->setEnabled(false);
    base64Row->addWidget(m_base64Label);
    base64Row->addStretch(1);
    base64Row->addWidget(m_copyBase64Button);
    right->addLayout(base64Row);

    m_base64Output = new QPlainTextEdit(this);
    m_base64Output->setReadOnly(true);
    m_base64Output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_base64Output->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_base64Output->setMinimumHeight(150);
    right->addWidget(m_base64Output, 1);

    auto *rulerRow = new QHBoxLayout();
    rulerRow->setSpacing(8);
    m_rulerLabel = new QLabel(this);
    m_rulerLabel->setObjectName(QStringLiteral("fieldLabel"));
    m_copyRulerButton = new QPushButton(this);
    m_copyRulerButton->setObjectName(QStringLiteral("secondaryButton"));
    m_copyRulerButton->setEnabled(false);
    rulerRow->addWidget(m_rulerLabel);
    rulerRow->addStretch(1);
    rulerRow->addWidget(m_copyRulerButton);
    right->addLayout(rulerRow);

    m_rulerOutput = new QPlainTextEdit(this);
    m_rulerOutput->setReadOnly(true);
    m_rulerOutput->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_rulerOutput->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_rulerOutput->setMinimumHeight(150);
    right->addWidget(m_rulerOutput, 1);

    body->addLayout(left, 1);
    body->addLayout(right, 1);
    root->addLayout(body, 1);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    m_closeButton = new QPushButton(this);
    m_closeButton->setObjectName(QStringLiteral("primaryButton"));
    m_closeButton->setDefault(true);
    buttonRow->addWidget(m_closeButton);
    root->addLayout(buttonRow);

    m_validationTimer = new QTimer(this);
    m_validationTimer->setSingleShot(true);
    m_validationTimer->setInterval(220);

    connect(m_inputEdit, &QPlainTextEdit::textChanged, this, [this] {
        m_validationTimer->start();
    });
    connect(m_validationTimer, &QTimer::timeout, this, &QuickConvertDialog::validateInput);
    connect(m_formatCombo, &QComboBox::currentIndexChanged, this, [this] {
        validateInput();
    });
    connect(m_copyBase64Button, &QPushButton::clicked, this, &QuickConvertDialog::copyBase64);
    connect(m_copyRulerButton, &QPushButton::clicked, this, &QuickConvertDialog::copyRuler);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void QuickConvertDialog::retranslateUi()
{
    setWindowTitle(m_i18n.t(QStringLiteral("quick.title")));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    m_inputLabel->setText(m_i18n.t(QStringLiteral("add.dna")));
    m_inputEdit->setPlaceholderText(m_i18n.t(QStringLiteral("add.dnaPlaceholder")));
    m_formatLabel->setText(m_i18n.t(QStringLiteral("add.format")));
    m_outputLabel->setText(m_i18n.t(QStringLiteral("quick.output")));
    m_base64Label->setText(m_i18n.t(QStringLiteral("detail.base64Tab")));
    m_rulerLabel->setText(m_i18n.t(QStringLiteral("detail.rulerTab")));
    m_copyBase64Button->setText(m_i18n.t(QStringLiteral("detail.copyBase64")));
    m_copyRulerButton->setText(m_i18n.t(QStringLiteral("detail.copyRuler")));
    m_closeButton->setText(m_i18n.t(QStringLiteral("msg.close")));

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

    if (m_inputEdit->toPlainText().trimmed().isEmpty())
        setStatus(m_i18n.t(QStringLiteral("quick.hint")), QStringLiteral("Muted"));
    else
        validateInput();
}

Base64Hint QuickConvertDialog::base64Hint() const
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

void QuickConvertDialog::validateInput()
{
    const QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        m_base64Output->clear();
        m_rulerOutput->clear();
        m_copyBase64Button->setEnabled(false);
        m_copyRulerButton->setEnabled(false);
        m_statusLabel->setToolTip(QString());
        setStatus(m_i18n.t(QStringLiteral("quick.hint")), QStringLiteral("Muted"));
        return;
    }

    const DnaParseResult result = DnaCodec::parse(
        text, base64Hint(), m_catalog, m_legacyCatalog);
    if (!result.ok) {
        m_base64Output->clear();
        m_rulerOutput->clear();
        m_copyBase64Button->setEnabled(false);
        m_copyRulerButton->setEnabled(false);
        m_statusLabel->setToolTip(QString());
        setStatus(result.error, QStringLiteral("Error"));
        return;
    }

    QString message;
    switch (result.format) {
    case DnaFormat::LegacyBase64:
        message = m_i18n.t(QStringLiteral("add.detectedLegacy")).arg(result.geneCount);
        break;
    case DnaFormat::RulerDesigner:
        message = m_i18n.t(QStringLiteral("add.detectedRuler")).arg(result.geneCount);
        break;
    case DnaFormat::ModernBase64:
    default:
        message = m_i18n.t(QStringLiteral("add.detectedModern")).arg(result.geneCount);
        break;
    }

    int modded = 0;
    int rulerOnly = 0;
    for (const ExtraGene &extra : result.data.extras) {
        if (extra.kind == ExtraGeneKind::RulerOnly)
            ++rulerOnly;
        else
            ++modded;
    }
    if (modded > 0)
        message += m_i18n.t(QStringLiteral("add.moddedSuffix")).arg(modded);
    if (rulerOnly > 0)
        message += m_i18n.t(QStringLiteral("add.rulerOnlySuffix")).arg(rulerOnly);

    QStringList warnings = result.warnings;
    if (!warnings.isEmpty()) {
        message += QStringLiteral(" ")
            + m_i18n.t(QStringLiteral("add.warnings")).arg(warnings.size());
        setStatus(message, QStringLiteral("Warning"));
    } else {
        setStatus(message, QStringLiteral("Ok"));
    }
    m_statusLabel->setToolTip(warnings.mid(0, 12).join(QLatin1Char('\n')));

    const QString base64 = DnaCodec::toBase64(result.data, m_catalog);
    const QString ruler = DnaCodec::toRuler(result.data, m_catalog);
    m_base64Output->setPlainText(base64);
    m_rulerOutput->setPlainText(ruler);
    m_copyBase64Button->setEnabled(!base64.isEmpty());
    m_copyRulerButton->setEnabled(!ruler.isEmpty());
}

void QuickConvertDialog::setStatus(const QString &text, const QString &state)
{
    m_statusLabel->setText(text);
    m_statusLabel->setObjectName(QStringLiteral("status") + state);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void QuickConvertDialog::copyBase64()
{
    const QString text = m_base64Output->toPlainText();
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
    setStatus(m_i18n.t(QStringLiteral("msg.copiedBase64")), QStringLiteral("Ok"));
}

void QuickConvertDialog::copyRuler()
{
    const QString text = m_rulerOutput->toPlainText();
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
    setStatus(m_i18n.t(QStringLiteral("msg.copiedRuler")), QStringLiteral("Ok"));
}

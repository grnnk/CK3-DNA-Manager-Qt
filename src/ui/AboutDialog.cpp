#include "ui/AboutDialog.h"

#include "core/I18n.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

AboutDialog::AboutDialog(I18n &i18n, QWidget *parent)
    : QDialog(parent)
    , m_i18n(i18n)
{
    setWindowTitle(m_i18n.t(QStringLiteral("about.title")));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    setMinimumWidth(560);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 22, 26, 20);
    root->setSpacing(10);

    auto *header = new QHBoxLayout();
    header->setSpacing(16);

    auto *icon = new QLabel(this);
    icon->setPixmap(QPixmap(QStringLiteral(":/icons/app.png"))
                        .scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    icon->setFixedSize(72, 72);
    header->addWidget(icon);

    auto *titleBox = new QVBoxLayout();
    titleBox->setSpacing(2);
    m_titleLabel = new QLabel(m_i18n.t(QStringLiteral("app.title")), this);
    m_titleLabel->setObjectName(QStringLiteral("aboutTitle"));
    m_versionLabel = new QLabel(
        m_i18n.t(QStringLiteral("about.version")).arg(QApplication::applicationVersion()), this);
    m_versionLabel->setObjectName(QStringLiteral("mutedLabel"));
    titleBox->addWidget(m_titleLabel);
    titleBox->addWidget(m_versionLabel);
    titleBox->addStretch(1);
    header->addLayout(titleBox, 1);

    root->addLayout(header);

    auto *tagline = new QLabel(m_i18n.t(QStringLiteral("about.tagline")), this);
    tagline->setObjectName(QStringLiteral("aboutTagline"));
    tagline->setWordWrap(true);
    root->addWidget(tagline);

    m_descriptionLabel = new QLabel(m_i18n.t(QStringLiteral("about.description")), this);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setObjectName(QStringLiteral("aboutText"));
    root->addWidget(m_descriptionLabel);

    m_creditsLabel = new QLabel(
        m_i18n.t(QStringLiteral("about.repository"))
            + QStringLiteral("<br><a href=\"https://github.com/grnnk/CK3-DNA-Manager-Qt\">"
                             "github.com/grnnk/CK3-DNA-Manager-Qt</a><br><br>")
            + m_i18n.t(QStringLiteral("about.original"))
            + QStringLiteral("<br><a href=\"https://github.com/Welshtokki/CK3_DNA_Manager\">"
                             "github.com/Welshtokki/CK3_DNA_Manager</a><br><br>")
            + m_i18n.t(QStringLiteral("about.qt"))
            + QStringLiteral("<br><a href=\"https://www.qt.io\">qt.io</a>"),
        this);
    m_creditsLabel->setWordWrap(true);
    m_creditsLabel->setTextFormat(Qt::RichText);
    m_creditsLabel->setOpenExternalLinks(true);
    m_creditsLabel->setObjectName(QStringLiteral("aboutText"));
    root->addWidget(m_creditsLabel);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    m_closeButton = new QPushButton(m_i18n.t(QStringLiteral("msg.close")), this);
    m_closeButton->setObjectName(QStringLiteral("primaryButton"));
    buttonRow->addWidget(m_closeButton);
    root->addLayout(buttonRow);

    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    resize(600, 400);
}

#include "ui/MainWindow.h"

#include "core/DnaCodec.h"
#include "core/ImageUtils.h"
#include "core/LegacyImporter.h"
#include "ui/AboutDialog.h"
#include "ui/AddEntryDialog.h"
#include "ui/EntryListDelegate.h"
#include "ui/FlowLayout.h"
#include "ui/GeneEditorWidget.h"
#include "ui/PortraitWidget.h"
#include "ui/QuickConvertDialog.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCollator>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

void migrateLegacyDatabase(const QString &targetDirectory)
{
#ifdef Q_OS_WIN
    const QString targetDatabase = QDir(targetDirectory).filePath(QStringLiteral("dna.db"));
    if (QFileInfo::exists(targetDatabase))
        return;

    const QString legacyDirectory = QFileInfo(targetDirectory).dir().filePath(
        QStringLiteral("Welshtokki/CK3 DNA Manager"));
    const QString legacyDatabase = QDir(legacyDirectory).filePath(QStringLiteral("dna.db"));
    if (!QFileInfo::exists(legacyDatabase))
        return;

    for (const QString &suffix : { QString(), QStringLiteral("-wal"), QStringLiteral("-shm") }) {
        const QString source = legacyDatabase + suffix;
        if (QFileInfo::exists(source))
            QFile::copy(source, targetDatabase + suffix);
    }
#else
    Q_UNUSED(targetDirectory)
#endif
}

QString sortKey(MainWindow * /*window*/, int sort)
{
    switch (sort) {
    case 1:
        return QStringLiteral("created");
    case 2:
        return QStringLiteral("name");
    default:
        return QStringLiteral("updated");
    }
}

QString systemLanguage()
{
    const QString name = QLocale::system().name().left(2).toLower();
    return name.isEmpty() ? QStringLiteral("en") : name;
}

QIcon actionIcon(const QString &name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
}

void fitWrappedLabel(QLabel *label, int width)
{
    if (!label)
        return;
    label->ensurePolished();
    const int height = label->heightForWidth(width);
    if (height > 0)
        label->setFixedHeight(height);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    setMinimumSize(1080, 700);
    resize(1320, 860);
}

bool MainWindow::initialize(QString *error)
{
    if (!m_i18n.load(QStringLiteral(":/i18n/translations.json"), error))
        return false;

    QSettings settings;
    QString language = settings.value(QStringLiteral("language")).toString();
    if (language.isEmpty())
        language = systemLanguage();
    if (!m_i18n.hasLanguage(language))
        language = QStringLiteral("en");
    m_i18n.setLanguage(language);

    if (!m_catalog.load(QStringLiteral(":/data/genes.json"), error))
        return false;
    if (!m_legacyCatalog.load(QStringLiteral(":/data/legacy_genes.json"), error))
        return false;

    const QString directory = dataDirectory();
    migrateLegacyDatabase(directory);
    const QString databasePath = QDir(directory).filePath(QStringLiteral("dna.db"));
    if (!m_database.open(databasePath, error))
        return false;

    const QString storedSort = settings.value(QStringLiteral("sort"),
                                              QStringLiteral("updated")).toString();
    if (storedSort == QStringLiteral("created"))
        m_sort = EntrySort::Created;
    else if (storedSort == QStringLiteral("name"))
        m_sort = EntrySort::Name;
    else
        m_sort = EntrySort::Updated;

    setupUi();
    setupMenus();
    applyTheme();
    retranslateUi();
    loadSettings();
    scheduleInitialLoad();

    statusBar()->showMessage(m_i18n.t(QStringLiteral("status.ready")), 4000);
    return true;
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *sidebar = new QFrame(central);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(334);
    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(16, 18, 16, 16);
    sideLayout->setSpacing(10);

    auto *brandRow = new QHBoxLayout();
    brandRow->setSpacing(10);
    auto *brandIcon = new QLabel(sidebar);
    brandIcon->setPixmap(QPixmap(QStringLiteral(":/icons/app.png"))
                             .scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brandIcon->setFixedSize(42, 42);
    auto *brandBox = new QVBoxLayout();
    brandBox->setSpacing(0);
    m_brandLabel = new QLabel(sidebar);
    m_brandLabel->setObjectName(QStringLiteral("appBrand"));
    m_libraryTitle = new QLabel(sidebar);
    m_libraryTitle->setObjectName(QStringLiteral("mutedLabel"));
    brandBox->addWidget(m_brandLabel);
    brandBox->addWidget(m_libraryTitle);
    brandRow->addWidget(brandIcon);
    brandRow->addLayout(brandBox, 1);
    sideLayout->addLayout(brandRow);

    m_countLabel = new QLabel(sidebar);
    m_countLabel->setObjectName(QStringLiteral("mutedLabel"));
    sideLayout->addWidget(m_countLabel);

    m_searchEdit = new QLineEdit(sidebar);
    m_searchEdit->setClearButtonEnabled(true);
    sideLayout->addWidget(m_searchEdit);

    auto *sortRow = new QHBoxLayout();
    sortRow->setSpacing(8);
    m_sortLabel = new QLabel(sidebar);
    m_sortLabel->setObjectName(QStringLiteral("fieldLabel"));
    m_sortCombo = new QComboBox(sidebar);
    m_sortCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sortRow->addWidget(m_sortLabel);
    sortRow->addWidget(m_sortCombo, 1);
    sideLayout->addLayout(sortRow);

    m_list = new QListWidget(sidebar);
    m_list->setObjectName(QStringLiteral("entryList"));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMouseTracking(true);
    m_list->setUniformItemSizes(true);
    m_list->setSpacing(2);
    m_listDelegate = new EntryListDelegate(m_list);
    m_list->setItemDelegate(m_listDelegate);

    auto *emptyPage = new QWidget(sidebar);
    auto *emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setContentsMargins(18, 24, 18, 24);
    emptyLayout->setSpacing(8);
    emptyLayout->addStretch(1);
    m_listEmptyTitle = new QLabel(emptyPage);
    m_listEmptyTitle->setObjectName(QStringLiteral("emptyTitle"));
    m_listEmptyTitle->setAlignment(Qt::AlignCenter);
    m_listEmptyTitle->setWordWrap(true);
    m_listEmptyHint = new QLabel(emptyPage);
    m_listEmptyHint->setObjectName(QStringLiteral("mutedLabel"));
    m_listEmptyHint->setAlignment(Qt::AlignCenter);
    m_listEmptyHint->setWordWrap(true);
    emptyLayout->addWidget(m_listEmptyTitle);
    emptyLayout->addWidget(m_listEmptyHint);
    emptyLayout->addStretch(2);

    m_listStack = new QStackedWidget(sidebar);
    m_listStack->addWidget(m_list);
    m_listStack->addWidget(emptyPage);
    sideLayout->addWidget(m_listStack, 1);

    auto *divider = new QFrame(sidebar);
    divider->setObjectName(QStringLiteral("divider"));
    divider->setFrameShape(QFrame::HLine);
    sideLayout->addWidget(divider);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    m_newButton = new QPushButton(sidebar);
    m_newButton->setObjectName(QStringLiteral("primaryButton"));
    m_newButton->setIcon(actionIcon(QStringLiteral("add")));
    m_importButton = new QPushButton(sidebar);
    m_importButton->setObjectName(QStringLiteral("secondaryButton"));
    m_importButton->setIcon(actionIcon(QStringLiteral("import")));
    buttonRow->addWidget(m_newButton);
    buttonRow->addWidget(m_importButton);
    sideLayout->addLayout(buttonRow);

    m_quickConvertButton = new QPushButton(sidebar);
    m_quickConvertButton->setObjectName(QStringLiteral("secondaryButton"));
    m_quickConvertButton->setIcon(actionIcon(QStringLiteral("convert")));
    sideLayout->addWidget(m_quickConvertButton);

    auto *languageRow = new QHBoxLayout();
    languageRow->setSpacing(8);
    m_languageLabel = new QLabel(sidebar);
    m_languageLabel->setObjectName(QStringLiteral("fieldLabel"));
    m_languageCombo = new QComboBox(sidebar);
    m_languageCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    languageRow->addWidget(m_languageLabel);
    languageRow->addWidget(m_languageCombo, 1);
    sideLayout->addLayout(languageRow);

    rootLayout->addWidget(sidebar);

    auto *mainPanel = new QFrame(central);
    mainPanel->setObjectName(QStringLiteral("mainPanel"));
    auto *mainLayout = new QVBoxLayout(mainPanel);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_mainStack = new QStackedWidget(mainPanel);

    m_welcomePage = new QWidget(m_mainStack);
    auto *welcomeLayout = new QVBoxLayout(m_welcomePage);
    welcomeLayout->setContentsMargins(30, 30, 30, 30);
    welcomeLayout->setSpacing(12);
    welcomeLayout->addStretch(1);

    auto *welcomeIcon = new QLabel(m_welcomePage);
    welcomeIcon->setPixmap(QPixmap(QStringLiteral(":/icons/app.png"))
                               .scaled(112, 112, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    welcomeIcon->setAlignment(Qt::AlignCenter);
    welcomeLayout->addWidget(welcomeIcon, 0, Qt::AlignHCenter);

    m_welcomeTitle = new QLabel(m_welcomePage);
    m_welcomeTitle->setObjectName(QStringLiteral("welcomeTitle"));
    m_welcomeTitle->setAlignment(Qt::AlignCenter);
    m_welcomeTitle->setWordWrap(true);
    m_welcomeTitle->setFixedWidth(560);
    welcomeLayout->addWidget(m_welcomeTitle, 0, Qt::AlignHCenter);

    m_welcomeHint = new QLabel(m_welcomePage);
    m_welcomeHint->setObjectName(QStringLiteral("mutedLabel"));
    m_welcomeHint->setAlignment(Qt::AlignCenter);
    m_welcomeHint->setWordWrap(true);
    m_welcomeHint->setFixedWidth(560);
    welcomeLayout->addWidget(m_welcomeHint, 0, Qt::AlignHCenter);

    auto *welcomeButtons = new QHBoxLayout();
    welcomeButtons->setSpacing(10);
    welcomeButtons->addStretch(1);
    m_welcomeNewButton = new QPushButton(m_welcomePage);
    m_welcomeNewButton->setObjectName(QStringLiteral("primaryButton"));
    m_welcomeNewButton->setIcon(actionIcon(QStringLiteral("add")));
    m_welcomeImportButton = new QPushButton(m_welcomePage);
    m_welcomeImportButton->setObjectName(QStringLiteral("secondaryButton"));
    m_welcomeImportButton->setIcon(actionIcon(QStringLiteral("import")));
    welcomeButtons->addWidget(m_welcomeNewButton);
    welcomeButtons->addWidget(m_welcomeImportButton);
    welcomeButtons->addStretch(1);
    welcomeLayout->addLayout(welcomeButtons);
    welcomeLayout->addStretch(2);

    m_contentPage = new QWidget(m_mainStack);
    auto *contentLayout = new QVBoxLayout(m_contentPage);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto *header = new QFrame(m_contentPage);
    header->setObjectName(QStringLiteral("detailHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(22, 14, 22, 10);
    headerLayout->setSpacing(10);
    m_detailTitle = new QLabel(header);
    m_detailTitle->setObjectName(QStringLiteral("detailTitle"));
    m_detailTitle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_detailTitle->setMinimumWidth(0);
    m_favoriteButton = new QToolButton(header);
    m_favoriteButton->setObjectName(QStringLiteral("favoriteButton"));
    m_favoriteButton->setCheckable(true);
    m_favoriteButton->setAutoRaise(true);
    m_favoriteButton->setFixedSize(38, 34);
    headerLayout->addWidget(m_detailTitle, 1);
    headerLayout->addWidget(m_favoriteButton);
    contentLayout->addWidget(header);

    m_tabs = new QTabWidget(m_contentPage);
    m_tabs->setObjectName(QStringLiteral("mainTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setMovable(false);

    auto *overviewScroll = new QScrollArea(m_tabs);
    overviewScroll->setWidgetResizable(true);
    overviewScroll->setFrameShape(QFrame::NoFrame);
    overviewScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *overviewContent = new QWidget(overviewScroll);
    overviewContent->setObjectName(QStringLiteral("overviewContent"));
    auto *overviewLayout = new QVBoxLayout(overviewContent);
    overviewLayout->setContentsMargins(22, 16, 22, 22);
    overviewLayout->setSpacing(16);

    auto *topLayout = new QHBoxLayout();
    topLayout->setSpacing(24);

    auto *portraitColumn = new QWidget(overviewContent);
    portraitColumn->setFixedWidth(330);
    auto *portraitLayout = new QVBoxLayout(portraitColumn);
    portraitLayout->setContentsMargins(0, 0, 0, 0);
    portraitLayout->setSpacing(8);
    m_portrait = new PortraitWidget(portraitColumn);
    m_portrait->setMinimumHeight(360);
    portraitLayout->addWidget(m_portrait, 1);

    auto *portraitButtons = new FlowLayout(nullptr, 0, 8, 8);
    m_changePortraitButton = new QPushButton(portraitColumn);
    m_changePortraitButton->setObjectName(QStringLiteral("secondaryButton"));
    m_changePortraitButton->setIcon(actionIcon(QStringLiteral("image")));
    m_pastePortraitButton = new QPushButton(portraitColumn);
    m_pastePortraitButton->setObjectName(QStringLiteral("secondaryButton"));
    m_pastePortraitButton->setIcon(actionIcon(QStringLiteral("paste")));
    m_removePortraitButton = new QPushButton(portraitColumn);
    m_removePortraitButton->setObjectName(QStringLiteral("secondaryButton"));
    m_removePortraitButton->setIcon(actionIcon(QStringLiteral("delete")));
    portraitButtons->addWidget(m_changePortraitButton);
    portraitButtons->addWidget(m_pastePortraitButton);
    portraitButtons->addWidget(m_removePortraitButton);
    portraitLayout->addLayout(portraitButtons);
    topLayout->addWidget(portraitColumn);

    auto *detailColumn = new QWidget(overviewContent);
    auto *detailLayout = new QVBoxLayout(detailColumn);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(8);

    m_nameLabel = new QLabel(detailColumn);
    m_nameLabel->setObjectName(QStringLiteral("fieldLabel"));
    m_nameEdit = new QLineEdit(detailColumn);
    m_nameEdit->setClearButtonEnabled(true);
    m_nameEdit->setObjectName(QStringLiteral("nameEdit"));
    detailLayout->addWidget(m_nameLabel);
    detailLayout->addWidget(m_nameEdit);

    m_notesLabel = new QLabel(detailColumn);
    m_notesLabel->setObjectName(QStringLiteral("fieldLabel"));
    m_notesEdit = new QTextEdit(detailColumn);
    m_notesEdit->setMinimumHeight(82);
    m_notesEdit->setMaximumHeight(120);
    m_notesEdit->setAcceptRichText(false);
    detailLayout->addWidget(m_notesLabel);
    detailLayout->addWidget(m_notesEdit);

    m_metaLabel = new QLabel(detailColumn);
    m_metaLabel->setObjectName(QStringLiteral("mutedLabel"));
    m_metaLabel->setWordWrap(true);
    m_metaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(m_metaLabel);

    auto *actionsLayout = new FlowLayout(nullptr, 0, 8, 8);

    m_editGenesButton = new QPushButton(detailColumn);
    m_editGenesButton->setObjectName(QStringLiteral("primaryButton"));
    m_editGenesButton->setIcon(actionIcon(QStringLiteral("edit")));
    m_copyBase64Button = new QPushButton(detailColumn);
    m_copyBase64Button->setObjectName(QStringLiteral("secondaryButton"));
    m_copyBase64Button->setIcon(actionIcon(QStringLiteral("copy")));
    m_copyRulerButton = new QPushButton(detailColumn);
    m_copyRulerButton->setObjectName(QStringLiteral("secondaryButton"));
    m_copyRulerButton->setIcon(actionIcon(QStringLiteral("copy")));
    m_saveDetailsButton = new QPushButton(detailColumn);
    m_saveDetailsButton->setObjectName(QStringLiteral("secondaryButton"));
    m_saveDetailsButton->setIcon(actionIcon(QStringLiteral("save")));
    m_duplicateButton = new QPushButton(detailColumn);
    m_duplicateButton->setObjectName(QStringLiteral("secondaryButton"));
    m_duplicateButton->setIcon(actionIcon(QStringLiteral("duplicate")));
    m_exportButton = new QPushButton(detailColumn);
    m_exportButton->setObjectName(QStringLiteral("secondaryButton"));
    m_exportButton->setIcon(actionIcon(QStringLiteral("export")));
    m_deleteButton = new QPushButton(detailColumn);
    m_deleteButton->setObjectName(QStringLiteral("dangerButton"));
    m_deleteButton->setIcon(actionIcon(QStringLiteral("delete")));

    for (QPushButton *button : { m_editGenesButton, m_copyBase64Button, m_copyRulerButton,
                                 m_saveDetailsButton, m_duplicateButton, m_exportButton,
                                 m_deleteButton }) {
        button->setMinimumHeight(34);
        button->setIconSize(QSize(16, 16));
    }

    actionsLayout->addWidget(m_editGenesButton);
    actionsLayout->addWidget(m_copyBase64Button);
    actionsLayout->addWidget(m_copyRulerButton);
    actionsLayout->addWidget(m_saveDetailsButton);
    actionsLayout->addWidget(m_duplicateButton);
    actionsLayout->addWidget(m_exportButton);
    actionsLayout->addWidget(m_deleteButton);
    detailLayout->addLayout(actionsLayout);

    m_codeTabs = new QTabWidget(detailColumn);
    m_codeTabs->setObjectName(QStringLiteral("codeTabs"));
    m_codeTabs->setDocumentMode(true);
    m_codeTabs->setMinimumHeight(210);
    m_base64Edit = new QPlainTextEdit(m_codeTabs);
    m_base64Edit->setReadOnly(true);
    m_base64Edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_base64Edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_rulerEdit = new QPlainTextEdit(m_codeTabs);
    m_rulerEdit->setReadOnly(true);
    m_rulerEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_rulerEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_codeTabs->addTab(m_base64Edit, QString());
    m_codeTabs->addTab(m_rulerEdit, QString());
    m_copyCodeButton = new QPushButton(m_codeTabs);
    m_copyCodeButton->setObjectName(QStringLiteral("secondaryButton"));
    m_copyCodeButton->setIcon(actionIcon(QStringLiteral("copy")));
    m_codeTabs->setCornerWidget(m_copyCodeButton, Qt::TopRightCorner);
    detailLayout->addWidget(m_codeTabs, 1);

    topLayout->addWidget(detailColumn, 1);
    overviewLayout->addLayout(topLayout);

    overviewScroll->setWidget(overviewContent);
    m_tabs->addTab(overviewScroll, QString());

    m_geneEditor = new GeneEditorWidget(m_catalog, m_i18n, m_tabs);
    m_geneEditor->setObjectName(QStringLiteral("geneEditor"));
    m_tabs->addTab(m_geneEditor, QString());

    contentLayout->addWidget(m_tabs, 1);

    m_mainStack->addWidget(m_welcomePage);
    m_mainStack->addWidget(m_contentPage);
    mainLayout->addWidget(m_mainStack);

    rootLayout->addWidget(mainPanel, 1);

    statusBar()->setSizeGripEnabled(false);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] { applyListFilter(); });
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading)
            return;
        const QString key = m_sortCombo->currentData().toString();
        if (key == QStringLiteral("created"))
            m_sort = EntrySort::Created;
        else if (key == QStringLiteral("name"))
            m_sort = EntrySort::Name;
        else
            m_sort = EntrySort::Updated;
        sortEntries();
        reloadEntryList(m_hasCurrent ? m_current.id : QString());
        QSettings settings;
        settings.setValue(QStringLiteral("sort"),
                          m_sort == EntrySort::Created ? QStringLiteral("created")
                          : m_sort == EntrySort::Name ? QStringLiteral("name")
                                                      : QStringLiteral("updated"));
    });
    connect(m_languageCombo, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading)
            return;
        changeLanguage(m_languageCombo->currentData().toString());
    });
    connect(m_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current, QListWidgetItem *previous) {
                if (m_loading || !current)
                    return;
                const QString id = current->data(EntryRoles::EntryId).toString();
                if (m_hasCurrent && id == m_current.id)
                    return;

                if (!maybeSaveBeforeLeave()) {
                    if (previous) {
                        QSignalBlocker blocker(m_list);
                        m_list->setCurrentItem(previous);
                    }
                    return;
                }

                selectRowById(id);
                loadEntry(id);
            });

    connect(m_newButton, &QPushButton::clicked, this, &MainWindow::newEntry);
    connect(m_importButton, &QPushButton::clicked, this, &MainWindow::importLegacy);
    connect(m_quickConvertButton, &QPushButton::clicked, this, &MainWindow::quickConvert);
    connect(m_welcomeNewButton, &QPushButton::clicked, this, &MainWindow::newEntry);
    connect(m_welcomeImportButton, &QPushButton::clicked, this, &MainWindow::importLegacy);
    connect(m_favoriteButton, &QToolButton::toggled, this, &MainWindow::toggleFavorite);
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this] {
        if (m_loading)
            return;
        m_detailsDirty = true;
        updateHeader();
        updateSaveButton();
    });
    connect(m_notesEdit, &QTextEdit::textChanged, this, [this] {
        if (m_loading)
            return;
        m_detailsDirty = true;
        updateSaveButton();
    });
    connect(m_saveDetailsButton, &QPushButton::clicked, this, [this] { saveEntry(true); });
    connect(m_editGenesButton, &QPushButton::clicked, this, [this] { m_tabs->setCurrentIndex(1); });
    connect(m_copyBase64Button, &QPushButton::clicked, this, &MainWindow::copyBase64);
    connect(m_copyRulerButton, &QPushButton::clicked, this, &MainWindow::copyRuler);
    connect(m_copyCodeButton, &QPushButton::clicked, this, &MainWindow::copyCurrentCodeTab);
    connect(m_duplicateButton, &QPushButton::clicked, this, &MainWindow::duplicateCurrent);
    connect(m_exportButton, &QPushButton::clicked, this, &MainWindow::exportCurrent);
    connect(m_deleteButton, &QPushButton::clicked, this, &MainWindow::deleteCurrent);
    connect(m_changePortraitButton, &QPushButton::clicked, this, &MainWindow::choosePortrait);
    connect(m_pastePortraitButton, &QPushButton::clicked, this, &MainWindow::pastePortrait);
    connect(m_removePortraitButton, &QPushButton::clicked, this, &MainWindow::removePortrait);
    connect(m_portrait, &PortraitWidget::chooseRequested, this, &MainWindow::choosePortrait);
    connect(m_portrait, &PortraitWidget::imageDropped, this, &MainWindow::applyPortrait);
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            &MainWindow::updatePortraitButtons);

    // Ctrl+V on any text input should paste a screenshot portrait when the
    // clipboard contains only an image. Text paste is unaffected.
    for (QWidget *widget : findChildren<QWidget *>()) {
        if (qobject_cast<QLineEdit *>(widget)
            || qobject_cast<QPlainTextEdit *>(widget)
            || qobject_cast<QTextEdit *>(widget)) {
            widget->installEventFilter(this);
        }
    }
    connect(m_geneEditor, &GeneEditorWidget::saveRequested, this, [this] { saveEntry(true); });
    connect(m_geneEditor, &GeneEditorWidget::revertRequested, this, [this] {
        m_geneEditor->revert();
        updateSaveButton();
    });
    connect(m_geneEditor, &GeneEditorWidget::dirtyChanged, this, [this](bool) {
        updateSaveButton();
    });
}

void MainWindow::setupMenus()
{
    menuBar()->setNativeMenuBar(false);

    m_fileMenu = menuBar()->addMenu(QString());
    m_newAction = m_fileMenu->addAction(actionIcon(QStringLiteral("add")), QString());
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newEntry);

    m_quickConvertAction = m_fileMenu->addAction(actionIcon(QStringLiteral("convert")), QString());
    connect(m_quickConvertAction, &QAction::triggered, this, &MainWindow::quickConvert);

    m_importLegacyAction = m_fileMenu->addAction(actionIcon(QStringLiteral("import")), QString());
    m_importLegacyAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    connect(m_importLegacyAction, &QAction::triggered, this, &MainWindow::importLegacy);

    m_fileMenu->addSeparator();
    m_exportAction = m_fileMenu->addAction(actionIcon(QStringLiteral("export")), QString());
    m_exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportCurrent);

    m_exportAllAction = m_fileMenu->addAction(QString());
    m_exportAllAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    connect(m_exportAllAction, &QAction::triggered, this, &MainWindow::exportAll);

    m_fileMenu->addSeparator();
    m_openDataFolderAction = m_fileMenu->addAction(actionIcon(QStringLiteral("folder")), QString());
    connect(m_openDataFolderAction, &QAction::triggered, this, &MainWindow::openDataFolder);

    m_fileMenu->addSeparator();
    m_quitAction = m_fileMenu->addAction(QString());
    m_quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_editMenu = menuBar()->addMenu(QString());
    m_saveAction = m_editMenu->addAction(actionIcon(QStringLiteral("save")), QString());
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, [this] { saveEntry(true); });

    m_editMenu->addSeparator();
    m_copyBase64Action = m_editMenu->addAction(actionIcon(QStringLiteral("copy")), QString());
    m_copyBase64Action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    connect(m_copyBase64Action, &QAction::triggered, this, &MainWindow::copyBase64);

    m_copyRulerAction = m_editMenu->addAction(actionIcon(QStringLiteral("copy")), QString());
    m_copyRulerAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    connect(m_copyRulerAction, &QAction::triggered, this, &MainWindow::copyRuler);

    m_editMenu->addSeparator();
    m_duplicateAction = m_editMenu->addAction(actionIcon(QStringLiteral("duplicate")), QString());
    m_duplicateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(m_duplicateAction, &QAction::triggered, this, &MainWindow::duplicateCurrent);

    m_deleteAction = m_editMenu->addAction(actionIcon(QStringLiteral("delete")), QString());
    connect(m_deleteAction, &QAction::triggered, this, &MainWindow::deleteCurrent);

    m_viewMenu = menuBar()->addMenu(QString());
    m_findAction = m_viewMenu->addAction(actionIcon(QStringLiteral("search")), QString());
    m_findAction->setShortcut(QKeySequence::Find);
    connect(m_findAction, &QAction::triggered, this, [this] {
        m_searchEdit->setFocus(Qt::ShortcutFocusReason);
        m_searchEdit->selectAll();
    });

    m_languageMenu = m_viewMenu->addMenu(QString());
    m_languageGroup = new QActionGroup(this);
    m_languageGroup->setExclusive(true);
    for (const QString &code : m_i18n.availableLanguages()) {
        QAction *action = m_languageMenu->addAction(m_i18n.languageName(code));
        action->setCheckable(true);
        action->setData(code);
        m_languageGroup->addAction(action);
        m_languageActions.insert(code, action);
        connect(action, &QAction::triggered, this, [this, code] { changeLanguage(code); });
    }

    m_helpMenu = menuBar()->addMenu(QString());
    m_aboutAction = m_helpMenu->addAction(actionIcon(QStringLiteral("info")), QString());
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::applyTheme()
{
    QFile file(QStringLiteral(":/theme.qss"));
    if (file.open(QIODevice::ReadOnly))
        setStyleSheet(QString::fromUtf8(file.readAll()));
}

void MainWindow::loadSettings()
{
    QSettings settings;
    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    m_lastImportDir = settings.value(QStringLiteral("dirs/import")).toString();
    m_lastExportDir = settings.value(QStringLiteral("dirs/export")).toString();
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("language"), m_i18n.currentLanguage());
    settings.setValue(QStringLiteral("sort"), sortKey(this, static_cast<int>(m_sort)));
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("dirs/import"), m_lastImportDir);
    settings.setValue(QStringLiteral("dirs/export"), m_lastExportDir);
}

void MainWindow::retranslateUi()
{
    setWindowTitle(m_i18n.t(QStringLiteral("app.title")));
    m_fileMenu->setTitle(m_i18n.t(QStringLiteral("menu.file")));
    m_editMenu->setTitle(m_i18n.t(QStringLiteral("menu.edit")));
    m_viewMenu->setTitle(m_i18n.t(QStringLiteral("menu.view")));
    m_helpMenu->setTitle(m_i18n.t(QStringLiteral("menu.help")));
    m_languageMenu->setTitle(m_i18n.t(QStringLiteral("app.language")));

    m_newAction->setText(m_i18n.t(QStringLiteral("action.newEntry")));
    m_quickConvertAction->setText(m_i18n.t(QStringLiteral("action.quickConvert")));
    m_importLegacyAction->setText(m_i18n.t(QStringLiteral("action.importLegacy")));
    m_exportAction->setText(m_i18n.t(QStringLiteral("action.exportEntry")));
    m_exportAllAction->setText(m_i18n.t(QStringLiteral("action.exportAll")));
    m_openDataFolderAction->setText(m_i18n.t(QStringLiteral("action.openDataFolder")));
    m_quitAction->setText(m_i18n.t(QStringLiteral("action.quit")));
    m_saveAction->setText(m_i18n.t(QStringLiteral("action.save")));
    m_copyBase64Action->setText(m_i18n.t(QStringLiteral("action.copyBase64")));
    m_copyRulerAction->setText(m_i18n.t(QStringLiteral("action.copyRuler")));
    m_duplicateAction->setText(m_i18n.t(QStringLiteral("action.duplicate")));
    m_deleteAction->setText(m_i18n.t(QStringLiteral("action.delete")));
    m_findAction->setText(m_i18n.t(QStringLiteral("action.find")));
    m_aboutAction->setText(m_i18n.t(QStringLiteral("action.about")));

    for (auto it = m_languageActions.cbegin(); it != m_languageActions.cend(); ++it) {
        it.value()->setText(m_i18n.languageName(it.key()));
        it.value()->setChecked(it.key() == m_i18n.currentLanguage());
    }

    m_brandLabel->setText(m_i18n.t(QStringLiteral("app.title")));
    m_libraryTitle->setText(m_i18n.t(QStringLiteral("library.title")));
    m_searchEdit->setPlaceholderText(m_i18n.t(QStringLiteral("library.searchPlaceholder")));
    m_sortLabel->setText(m_i18n.t(QStringLiteral("library.sort")));
    m_newButton->setText(m_i18n.t(QStringLiteral("library.new")));
    m_importButton->setText(m_i18n.t(QStringLiteral("library.import")));
    m_quickConvertButton->setText(m_i18n.t(QStringLiteral("quick.title")));
    m_languageLabel->setText(m_i18n.t(QStringLiteral("app.language")));

    {
        QSignalBlocker blocker(m_sortCombo);
        m_sortCombo->clear();
        m_sortCombo->addItem(m_i18n.t(QStringLiteral("library.sortUpdated")),
                             QStringLiteral("updated"));
        m_sortCombo->addItem(m_i18n.t(QStringLiteral("library.sortCreated")),
                             QStringLiteral("created"));
        m_sortCombo->addItem(m_i18n.t(QStringLiteral("library.sortName")),
                             QStringLiteral("name"));
        const QString key = sortKey(this, static_cast<int>(m_sort));
        const int index = m_sortCombo->findData(key);
        m_sortCombo->setCurrentIndex(index >= 0 ? index : 0);
    }

    {
        QSignalBlocker blocker(m_languageCombo);
        m_languageCombo->clear();
        for (const QString &code : m_i18n.availableLanguages())
            m_languageCombo->addItem(m_i18n.languageName(code), code);
        const int index = m_languageCombo->findData(m_i18n.currentLanguage());
        m_languageCombo->setCurrentIndex(index >= 0 ? index : 0);
    }

    m_welcomeTitle->setText(m_i18n.t(QStringLiteral("welcome.title")));
    m_welcomeHint->setText(m_i18n.t(QStringLiteral("welcome.hint")));
    fitWrappedLabel(m_welcomeTitle, 560);
    fitWrappedLabel(m_welcomeHint, 560);
    m_welcomeNewButton->setText(m_i18n.t(QStringLiteral("welcome.new")));
    m_welcomeImportButton->setText(m_i18n.t(QStringLiteral("welcome.import")));

    m_tabs->setTabText(0, m_i18n.t(QStringLiteral("detail.overview")));
    m_tabs->setTabText(1, m_i18n.t(QStringLiteral("detail.genes")));
    m_codeTabs->setTabText(0, m_i18n.t(QStringLiteral("detail.base64Tab")));
    m_codeTabs->setTabText(1, m_i18n.t(QStringLiteral("detail.rulerTab")));
    m_copyCodeButton->setText(m_i18n.t(QStringLiteral("detail.copy")));
    m_copyCodeButton->setIcon(actionIcon(QStringLiteral("copy")));

    m_nameLabel->setText(m_i18n.t(QStringLiteral("detail.name")));
    m_nameEdit->setPlaceholderText(m_i18n.t(QStringLiteral("detail.namePlaceholder")));
    m_notesLabel->setText(m_i18n.t(QStringLiteral("detail.notes")));
    m_notesEdit->setPlaceholderText(m_i18n.t(QStringLiteral("detail.notesPlaceholder")));
    m_changePortraitButton->setText(m_i18n.t(QStringLiteral("detail.changePortrait")));
    m_pastePortraitButton->setText(m_i18n.t(QStringLiteral("action.pasteImage")));
    m_pastePortraitButton->setToolTip(m_i18n.t(QStringLiteral("action.pasteImageHint")));
    m_removePortraitButton->setText(m_i18n.t(QStringLiteral("detail.removePortrait")));
    m_editGenesButton->setText(m_i18n.t(QStringLiteral("detail.editGenes")));
    m_copyBase64Button->setText(m_i18n.t(QStringLiteral("detail.copyBase64")));
    m_copyRulerButton->setText(m_i18n.t(QStringLiteral("detail.copyRuler")));
    m_saveDetailsButton->setText(m_i18n.t(QStringLiteral("detail.saveDetails")));
    m_duplicateButton->setText(m_i18n.t(QStringLiteral("detail.duplicate")));
    m_exportButton->setText(m_i18n.t(QStringLiteral("detail.export")));
    m_deleteButton->setText(m_i18n.t(QStringLiteral("detail.delete")));
    m_portrait->setPlaceholderText(m_i18n.t(QStringLiteral("detail.dropPortrait")));

    m_geneEditor->retranslateUi();

    updateHeader();
    updateMeta();
    updateFavoriteButton();
    updatePortraitButtons();
    updateActionStates();
    updateSaveButton();
    applyListFilter();
}

void MainWindow::refreshAll(const QString &selectId)
{
    m_initialLoaded = true;
    QString keep = selectId;
    if (keep.isEmpty() && m_hasCurrent)
        keep = m_current.id;

    QString error;
    const QVector<EntryRecord> entries = m_database.entries(&error);
    if (!error.isEmpty()) {
        showError(m_i18n.t(QStringLiteral("msg.databaseError")), error);
        m_entries.clear();
        rebuildList(QString());
        applyListFilter();
        clearEntry();
        return;
    }

    m_entries = entries;
    sortEntries();

    QString target = keep;
    const bool found = std::any_of(m_entries.cbegin(), m_entries.cend(),
                                   [&target](const EntryRecord &entry) {
                                       return entry.id == target;
                                   });
    if (!found && !m_entries.isEmpty())
        target = m_entries.first().id;

    rebuildList(target);
    applyListFilter();

    if (m_entries.isEmpty()) {
        clearEntry();
    } else {
        loadEntry(target);
    }
}

void MainWindow::reloadEntryList(const QString &selectId)
{
    QString error;
    const QVector<EntryRecord> entries = m_database.entries(&error);
    if (!error.isEmpty()) {
        showError(m_i18n.t(QStringLiteral("msg.databaseError")), error);
        return;
    }
    m_entries = entries;
    sortEntries();
    rebuildList(selectId);
    applyListFilter();
}

void MainWindow::sortEntries()
{
    QCollator collator(m_i18n.locale());
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);

    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [this, &collator](const EntryRecord &left, const EntryRecord &right) {
                         if (left.favorite != right.favorite)
                             return left.favorite;
                         switch (m_sort) {
                         case EntrySort::Created:
                             if (left.createdAt != right.createdAt)
                                 return left.createdAt > right.createdAt;
                             break;
                         case EntrySort::Name: {
                             const int result = collator.compare(left.name, right.name);
                             if (result != 0)
                                 return result < 0;
                             break;
                         }
                         case EntrySort::Updated:
                         default:
                             if (left.updatedAt != right.updatedAt)
                                 return left.updatedAt > right.updatedAt;
                             break;
                         }
                         return left.id < right.id;
                     });
}

void MainWindow::rebuildList(const QString &selectId)
{
    QSignalBlocker blocker(m_list);
    m_list->clear();

    QListWidgetItem *selected = nullptr;
    for (const EntryRecord &entry : m_entries) {
        auto *item = new QListWidgetItem();
        item->setData(EntryRoles::EntryId, entry.id);
        item->setData(EntryRoles::Name, entry.name);
        item->setData(EntryRoles::Subtitle, subtitleFor(entry));
        item->setData(EntryRoles::DateText, dateText(entry.updatedAt));
        item->setData(EntryRoles::Favorite, entry.favorite);
        item->setData(EntryRoles::SearchText, entry.name + QLatin1Char('\n') + entry.notes);

        if (!entry.thumbnail.isEmpty())
            item->setData(EntryRoles::ThumbnailBytes, entry.thumbnail);
        item->setSizeHint(QSize(0, 82));
        m_list->addItem(item);
        if (entry.id == selectId)
            selected = item;
    }

    if (selected) {
        m_list->setCurrentItem(selected);
    } else if (m_list->count() > 0) {
        m_list->setCurrentItem(m_list->item(0));
    }
    if (m_list->currentItem())
        m_list->scrollToItem(m_list->currentItem());
}

void MainWindow::applyListFilter()
{
    if (!m_list)
        return;

    const QString query = m_searchEdit->text().trimmed();
    int visible = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        const QString searchText = item->data(EntryRoles::SearchText).toString();
        const bool matches = query.isEmpty()
            || searchText.contains(query, Qt::CaseInsensitive);
        item->setHidden(!matches);
        if (matches)
            ++visible;
    }

    if (m_entries.isEmpty()) {
        m_listStack->setCurrentIndex(1);
        m_listEmptyTitle->setText(m_i18n.t(QStringLiteral("library.emptyTitle")));
        m_listEmptyHint->setText(m_i18n.t(QStringLiteral("library.emptyHint")));
    } else if (visible == 0) {
        m_listStack->setCurrentIndex(1);
        m_listEmptyTitle->setText(m_i18n.t(QStringLiteral("library.noMatches")));
        m_listEmptyHint->setText(m_i18n.t(QStringLiteral("library.noMatchesHint")));
    } else {
        m_listStack->setCurrentIndex(0);
    }

    updateCountLabel();
}

void MainWindow::updateCountLabel()
{
    if (m_entries.isEmpty()) {
        m_countLabel->setText(m_i18n.t(QStringLiteral("library.count")).arg(0));
        return;
    }
    if (m_searchEdit->text().trimmed().isEmpty()) {
        m_countLabel->setText(m_i18n.t(QStringLiteral("library.count")).arg(m_entries.size()));
        return;
    }
    int visible = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        if (!m_list->item(i)->isHidden())
            ++visible;
    }
    m_countLabel->setText(m_i18n.t(QStringLiteral("library.filteredCount"))
                              .arg(visible)
                              .arg(m_entries.size()));
}

bool MainWindow::selectRowById(const QString &id)
{
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        if (item->data(EntryRoles::EntryId).toString() == id) {
            QSignalBlocker blocker(m_list);
            m_list->setCurrentItem(item);
            m_list->scrollToItem(item);
            return true;
        }
    }
    return false;
}

void MainWindow::loadEntry(const QString &id)
{
    EntryRecord entry;
    QString error;
    if (!m_database.loadEntry(id, &entry, &error)) {
        showError(m_i18n.t(QStringLiteral("msg.databaseError")), error);
        return;
    }

    m_current = entry;
    m_hasCurrent = true;
    m_detailsDirty = false;

    DnaParseResult parsed = DnaCodec::fromBytes(
        entry.dna, Base64Hint::Modern, m_catalog, m_legacyCatalog);
    if (parsed.ok && !entry.rulerMeta.isEmpty())
        DnaCodec::applyRulerMetadata(parsed.data, entry.rulerMeta);
    m_currentDnaValid = parsed.ok;
    m_currentData = parsed.ok ? parsed.data : DnaData {};

    m_geneEditor->setEntryAvailable(parsed.ok);
    m_geneEditor->setData(m_currentData);

    m_loading = true;
    m_nameEdit->setText(entry.name);
    m_notesEdit->setPlainText(entry.notes);
    m_loading = false;

    m_mainStack->setCurrentWidget(m_contentPage);
    selectRowById(id);
    updateEntryView();

    if (!parsed.ok) {
        showError(m_i18n.t(QStringLiteral("msg.parseError")), parsed.error);
    }
}

void MainWindow::clearEntry()
{
    m_hasCurrent = false;
    m_currentDnaValid = false;
    m_current = {};
    m_currentData = {};
    m_detailsDirty = false;

    m_loading = true;
    if (m_nameEdit)
        m_nameEdit->clear();
    if (m_notesEdit)
        m_notesEdit->clear();
    m_loading = false;

    if (m_geneEditor) {
        m_geneEditor->setEntryAvailable(false);
        m_geneEditor->setData(DnaData {});
    }
    if (m_portrait)
        m_portrait->clearImage();
    if (m_base64Edit)
        m_base64Edit->clear();
    if (m_rulerEdit)
        m_rulerEdit->clear();
    if (m_metaLabel)
        m_metaLabel->clear();
    if (m_detailTitle)
        m_detailTitle->setText(m_i18n.t(QStringLiteral("detail.noSelection")));

    if (m_mainStack)
        m_mainStack->setCurrentWidget(m_welcomePage);
    setWindowTitle(m_i18n.t(QStringLiteral("app.title")));

    updateFavoriteButton();
    updatePortraitButtons();
    updateActionStates();
    updateSaveButton();
}

void MainWindow::updateEntryView()
{
    if (!m_hasCurrent)
        return;

    m_loading = true;
    m_nameEdit->setText(m_current.name);
    m_notesEdit->setPlainText(m_current.notes);
    m_loading = false;

    if (!m_current.portrait.isEmpty()) {
        const QImage image = ImageUtils::imageFromBytes(m_current.portrait);
        if (!image.isNull())
            m_portrait->setImage(image);
        else
            m_portrait->clearImage();
    } else {
        m_portrait->clearImage();
    }

    updateFavoriteButton();
    updateCodes();
    updateMeta();
    updateHeader();
    updatePortraitButtons();
    updateActionStates();
    updateSaveButton();
}

void MainWindow::updateCodes()
{
    if (!m_hasCurrent) {
        m_base64Edit->clear();
        m_base64Edit->setToolTip(QString());
        m_rulerEdit->clear();
        return;
    }

    m_base64Edit->setPlainText(QString::fromLatin1(m_current.dna.toBase64()));
    m_base64Edit->setToolTip(m_currentData.extras.isEmpty()
                                 ? QString()
                                 : m_i18n.t(QStringLiteral("genes.moddedHint")));
    if (m_currentDnaValid)
        m_rulerEdit->setPlainText(DnaCodec::toRuler(m_currentData, m_catalog));
    else
        m_rulerEdit->clear();
    m_copyCodeButton->setEnabled(!m_base64Edit->toPlainText().isEmpty()
                                 || !m_rulerEdit->toPlainText().isEmpty());
}

void MainWindow::updateMeta()
{
    if (!m_hasCurrent) {
        m_metaLabel->clear();
        return;
    }

    const int genes = m_current.dna.size() / 4;
    int modded = 0;
    int rulerOnly = 0;
    for (const ExtraGene &extra : m_currentData.extras) {
        if (extra.kind == ExtraGeneKind::RulerOnly)
            ++rulerOnly;
        else
            ++modded;
    }
    QString geneText = m_i18n.t(QStringLiteral("detail.genesCount")).arg(genes);
    if (modded > 0)
        geneText += QStringLiteral(" · ")
            + m_i18n.t(QStringLiteral("detail.moddedCount")).arg(modded);
    if (rulerOnly > 0)
        geneText += QStringLiteral(" · ")
            + m_i18n.t(QStringLiteral("detail.rulerOnlyCount")).arg(rulerOnly);

    const QStringList parts = {
        m_i18n.t(QStringLiteral("detail.created")) + QStringLiteral(": ")
            + dateText(m_current.createdAt),
        m_i18n.t(QStringLiteral("detail.updated")) + QStringLiteral(": ")
            + dateText(m_current.updatedAt),
        geneText,
    };
    m_metaLabel->setText(parts.join(QStringLiteral("    ·    ")));
}

void MainWindow::updateHeader()
{
    if (!m_hasCurrent) {
        if (m_detailTitle)
            m_detailTitle->setText(m_i18n.t(QStringLiteral("detail.noSelection")));
        setWindowTitle(m_i18n.t(QStringLiteral("app.title")));
        return;
    }

    QString name = m_nameEdit ? m_nameEdit->text().trimmed() : QString();
    if (name.isEmpty())
        name = m_current.name;
    m_detailTitle->setText(name);
    setWindowTitle(m_i18n.t(QStringLiteral("app.title")) + QStringLiteral(" — ") + name);
}

void MainWindow::updateFavoriteButton()
{
    if (!m_favoriteButton)
        return;
    const QSignalBlocker blocker(m_favoriteButton);
    const bool favorite = m_hasCurrent && m_current.favorite;
    m_favoriteButton->setChecked(favorite);
    m_favoriteButton->setText(favorite ? QStringLiteral("★") : QStringLiteral("☆"));
    m_favoriteButton->setToolTip(m_i18n.t(favorite
        ? QStringLiteral("detail.unfavorite")
        : QStringLiteral("detail.favorite")));
}

void MainWindow::updateSaveButton()
{
    if (!m_saveDetailsButton)
        return;
    const bool dirty = m_hasCurrent
        && (m_detailsDirty || (m_geneEditor && m_geneEditor->hasUnsavedChanges()));
    m_saveDetailsButton->setEnabled(dirty);
    if (m_saveAction)
        m_saveAction->setEnabled(m_hasCurrent);
}

void MainWindow::updatePortraitButtons()
{
    if (!m_changePortraitButton || !m_pastePortraitButton || !m_removePortraitButton)
        return;
    m_changePortraitButton->setEnabled(m_hasCurrent);
    m_pastePortraitButton->setEnabled(m_hasCurrent && ImageUtils::hasClipboardImage());
    m_removePortraitButton->setEnabled(m_hasCurrent && !m_current.portrait.isEmpty());
}

void MainWindow::updateActionStates()
{
    const bool current = m_hasCurrent;
    const bool dnaValid = current && m_currentDnaValid;
    m_exportAction->setEnabled(current);
    m_saveAction->setEnabled(current);
    m_copyBase64Action->setEnabled(current);
    m_copyRulerAction->setEnabled(dnaValid);
    m_duplicateAction->setEnabled(current);
    m_deleteAction->setEnabled(current);
    m_exportAllAction->setEnabled(!m_entries.isEmpty());
    m_favoriteButton->setEnabled(current);
    m_editGenesButton->setEnabled(dnaValid);
    m_copyBase64Button->setEnabled(current);
    m_copyRulerButton->setEnabled(dnaValid);
    m_duplicateButton->setEnabled(current);
    m_exportButton->setEnabled(current);
    m_deleteButton->setEnabled(current);
    m_copyCodeButton->setEnabled(current);
    updateSaveButton();
}

bool MainWindow::saveEntry(bool showMessage)
{
    if (!m_hasCurrent)
        return false;

    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        name = m_i18n.t(QStringLiteral("add.nameFallback"));
        m_loading = true;
        m_nameEdit->setText(name);
        m_loading = false;
    }

    m_current.name = name;
    m_current.notes = m_notesEdit->toPlainText();

    if (m_currentDnaValid) {
        const DnaData data = m_geneEditor->data();
        m_current.dna = DnaCodec::toBytes(data, m_catalog);
        m_current.rulerMeta = DnaCodec::serializeRulerMetadata(data);
        m_currentData = data;
    }
    m_current.updatedAt = QDateTime::currentMSecsSinceEpoch();

    QString error;
    if (!m_database.update(m_current, &error)) {
        showError(m_i18n.t(QStringLiteral("msg.databaseError")), error);
        return false;
    }

    m_detailsDirty = false;
    if (m_currentDnaValid)
        m_geneEditor->markSaved();

    updateCodes();
    updateMeta();
    updateHeader();
    updateSaveButton();
    updateFavoriteButton();
    reloadEntryList(m_current.id);

    if (showMessage)
        statusBar()->showMessage(m_i18n.t(QStringLiteral("msg.saved")), 3000);
    return true;
}

bool MainWindow::maybeSaveBeforeLeave()
{
    if (!m_hasCurrent)
        return true;

    const bool geneDirty = m_geneEditor && m_geneEditor->hasUnsavedChanges();
    if (!geneDirty) {
        if (m_detailsDirty)
            return saveEntry(false);
        return true;
    }

    QMessageBox box(QMessageBox::Question,
                    m_i18n.t(QStringLiteral("msg.unsavedTitle")),
                    m_i18n.t(QStringLiteral("msg.unsavedText")),
                    QMessageBox::NoButton, this);
    QPushButton *saveButton = box.addButton(m_i18n.t(QStringLiteral("msg.save")),
                                            QMessageBox::AcceptRole);
    box.addButton(m_i18n.t(QStringLiteral("msg.discard")), QMessageBox::DestructiveRole);
    QPushButton *cancelButton = box.addButton(m_i18n.t(QStringLiteral("msg.cancel")),
                                              QMessageBox::RejectRole);
    box.setDefaultButton(saveButton);
    box.setEscapeButton(cancelButton);
    box.exec();

    if (box.clickedButton() == saveButton)
        return saveEntry(false);
    if (box.clickedButton() == cancelButton || box.clickedButton() == nullptr)
        return false;
    return true;
}

void MainWindow::newEntry()
{
    newEntryWithImage(QImage());
}

void MainWindow::quickConvert()
{
    QuickConvertDialog dialog(m_catalog, m_legacyCatalog, m_i18n, this);
    dialog.exec();
}

void MainWindow::newEntryWithImage(const QImage &image)
{
    if (!maybeSaveBeforeLeave())
        return;

    AddEntryDialog dialog(m_catalog, m_legacyCatalog, m_i18n, this, image);
    if (dialog.exec() != QDialog::Accepted)
        return;

    EntryRecord entry = dialog.record();
    QString error;
    if (!m_database.insert(entry, &error)) {
        showError(m_i18n.t(QStringLiteral("msg.databaseError")), error);
        return;
    }

    refreshAll(entry.id);
    m_tabs->setCurrentIndex(0);
    statusBar()->showMessage(m_i18n.t(QStringLiteral("msg.saved")), 3000);
}

void MainWindow::importLegacy()
{
    if (!maybeSaveBeforeLeave())
        return;

    const QString directory = defaultDialogDirectory(QStringLiteral("import"));
    const QString path = QFileDialog::getOpenFileName(
        this, m_i18n.t(QStringLiteral("msg.importTitle")), directory,
        m_i18n.t(QStringLiteral("filter.json")));
    if (path.isEmpty())
        return;

    QProgressDialog progress(m_i18n.t(QStringLiteral("msg.importProgressTitle")),
                             QString(), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setCancelButton(nullptr);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setValue(0);

    const LegacyImportResult result = LegacyImporter::importFile(
        path, m_database, m_catalog, m_legacyCatalog,
        [this, &progress](int done, int total) {
            progress.setMaximum(std::max(total, 1));
            progress.setValue(done);
            progress.setLabelText(m_i18n.t(QStringLiteral("msg.importProgressText"))
                                      .arg(done)
                                      .arg(total));
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        });
    progress.close();

    m_lastImportDir = QFileInfo(path).absolutePath();

    if (!result.ok) {
        QString message = result.error;
        if (result.unsupportedFormat || result.unsupportedVersion) {
            message = m_i18n.t(QStringLiteral("msg.importUnsupported"));
        } else if (result.emptyFile) {
            message = m_i18n.t(QStringLiteral("msg.importEmpty"));
        }
        showError(m_i18n.t(QStringLiteral("msg.importTitle")), message);
        return;
    }

    refreshAll();
    if (!m_entries.isEmpty()) {
        selectRowById(m_entries.first().id);
        loadEntry(m_entries.first().id);
    }

    QString summary = m_i18n.t(QStringLiteral("msg.importSummary"))
                          .arg(result.imported)
                          .arg(result.portraits)
                          .arg(result.skipped);
    if (!result.warnings.isEmpty()) {
        summary += QStringLiteral("\n\n")
            + m_i18n.t(QStringLiteral("msg.importWarnings"))
                  .arg(result.warnings.mid(0, 8).join(QLatin1Char('\n')));
    }
    showInfo(m_i18n.t(QStringLiteral("msg.importTitle")), summary);
}

void MainWindow::exportCurrent()
{
    if (!m_hasCurrent || !maybeSaveBeforeLeave())
        return;

    EntryRecord entry;
    QString error;
    if (!m_database.loadEntry(m_current.id, &entry, &error)) {
        showError(m_i18n.t(QStringLiteral("msg.databaseError")), error);
        return;
    }
    exportRecords({ entry }, sanitizedFileName(entry.name));
}

void MainWindow::exportAll()
{
    if (!maybeSaveBeforeLeave())
        return;

    QVector<EntryRecord> records;
    for (const EntryRecord &entry : m_entries) {
        EntryRecord full;
        QString error;
        if (m_database.loadEntry(entry.id, &full, &error))
            records.append(full);
    }
    if (records.isEmpty()) {
        showInfo(m_i18n.t(QStringLiteral("msg.exportTitle")),
                 m_i18n.t(QStringLiteral("library.emptyTitle")));
        return;
    }

    exportRecords(records, sanitizedFileName(m_i18n.t(QStringLiteral("library.title"))));
}

void MainWindow::duplicateCurrent()
{
    if (!m_hasCurrent)
        return;
    if (!maybeSaveBeforeLeave())
        return;

    EntryRecord source;
    QString error;
    if (!m_database.loadEntry(m_current.id, &source, &error)) {
        showError(m_i18n.t(QStringLiteral("msg.databaseError")), error);
        return;
    }

    EntryRecord copy = source;
    copy.id.clear();
    copy.name = source.name + QStringLiteral(" (")
        + m_i18n.t(QStringLiteral("msg.duplicateSuffix")) + QLatin1Char(')');
    copy.createdAt = QDateTime::currentMSecsSinceEpoch();

    if (!m_database.insert(copy, &error)) {
        showError(m_i18n.t(QStringLiteral("msg.databaseError")), error);
        return;
    }

    refreshAll(copy.id);
    statusBar()->showMessage(m_i18n.t(QStringLiteral("msg.saved")), 3000);
}

void MainWindow::deleteCurrent()
{
    if (!m_hasCurrent)
        return;
    if (!askDelete(m_current.name))
        return;

    QString error;
    if (!m_database.remove(m_current.id, &error)) {
        showError(m_i18n.t(QStringLiteral("msg.databaseError")), error);
        return;
    }

    m_hasCurrent = false;
    m_current = {};
    refreshAll();
    statusBar()->showMessage(m_i18n.t(QStringLiteral("msg.deleted")), 3000);
}

void MainWindow::copyBase64()
{
    if (!m_hasCurrent || m_base64Edit->toPlainText().isEmpty())
        return;
    QGuiApplication::clipboard()->setText(m_base64Edit->toPlainText());
    statusBar()->showMessage(m_i18n.t(QStringLiteral("msg.copiedBase64")), 3000);
}

void MainWindow::copyRuler()
{
    if (!m_hasCurrent || m_rulerEdit->toPlainText().isEmpty())
        return;
    QGuiApplication::clipboard()->setText(m_rulerEdit->toPlainText());
    statusBar()->showMessage(m_i18n.t(QStringLiteral("msg.copiedRuler")), 3000);
}

void MainWindow::copyCurrentCodeTab()
{
    if (m_codeTabs->currentIndex() == 0)
        copyBase64();
    else
        copyRuler();
}

void MainWindow::choosePortrait()
{
    if (!m_hasCurrent)
        return;

    const QString directory = m_lastExportDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
        : m_lastExportDir;
    const QString path = QFileDialog::getOpenFileName(
        this, m_i18n.t(QStringLiteral("detail.changePortrait")), directory,
        m_i18n.t(QStringLiteral("filter.images")));
    if (path.isEmpty())
        return;

    QString error;
    const QImage image = ImageUtils::loadImage(path, &error);
    if (image.isNull()) {
        showError(m_i18n.t(QStringLiteral("msg.error")),
                  m_i18n.t(QStringLiteral("msg.imageError")).arg(error));
        return;
    }
    applyPortrait(image);
}

void MainWindow::pastePortrait()
{
    const QImage image = ImageUtils::imageFromClipboard();
    if (image.isNull())
        return;

    if (!m_hasCurrent) {
        newEntryWithImage(image);
        return;
    }
    applyPortrait(image);
}

void MainWindow::applyPortrait(const QImage &image)
{
    if (!m_hasCurrent || image.isNull())
        return;

    m_current.portrait = ImageUtils::encodePng(image);
    m_current.thumbnail = ImageUtils::makeThumbnail(image);
    m_current.updatedAt = QDateTime::currentMSecsSinceEpoch();

    QString error;
    if (!m_database.update(m_current, &error)) {
        showError(m_i18n.t(QStringLiteral("msg.databaseError")), error);
        return;
    }

    m_portrait->setImage(image);
    updatePortraitButtons();
    updateMeta();
    if (m_listDelegate)
        m_listDelegate->clearCache(m_current.id);
    reloadEntryList(m_current.id);
    statusBar()->showMessage(m_i18n.t(QStringLiteral("msg.portraitUpdated")), 3000);
}

void MainWindow::removePortrait()
{
    if (!m_hasCurrent || m_current.portrait.isEmpty())
        return;

    m_current.portrait.clear();
    m_current.thumbnail.clear();
    m_current.updatedAt = QDateTime::currentMSecsSinceEpoch();

    QString error;
    if (!m_database.update(m_current, &error)) {
        showError(m_i18n.t(QStringLiteral("msg.databaseError")), error);
        return;
    }

    m_portrait->clearImage();
    updatePortraitButtons();
    updateMeta();
    if (m_listDelegate)
        m_listDelegate->clearCache(m_current.id);
    reloadEntryList(m_current.id);
    statusBar()->showMessage(m_i18n.t(QStringLiteral("msg.portraitRemoved")), 3000);
}

void MainWindow::toggleFavorite(bool favorite)
{
    if (!m_hasCurrent)
        return;

    m_current.favorite = favorite;
    m_current.updatedAt = QDateTime::currentMSecsSinceEpoch();

    QString error;
    if (!m_database.update(m_current, &error)) {
        showError(m_i18n.t(QStringLiteral("msg.databaseError")), error);
        updateFavoriteButton();
        return;
    }

    updateFavoriteButton();
    updateMeta();
    reloadEntryList(m_current.id);
}

void MainWindow::changeLanguage(const QString &code)
{
    if (code == m_i18n.currentLanguage() || !m_i18n.hasLanguage(code))
        return;

    m_i18n.setLanguage(code);
    QSettings settings;
    settings.setValue(QStringLiteral("language"), code);

    retranslateUi();
    if (m_hasCurrent) {
        updateCodes();
        updateMeta();
        updateHeader();
    }
    reloadEntryList(m_hasCurrent ? m_current.id : QString());
}

void MainWindow::showAbout()
{
    AboutDialog dialog(m_i18n, this);
    dialog.exec();
}

void MainWindow::openDataFolder()
{
    const QString folder = QFileInfo(m_database.filePath()).absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder))) {
        showError(m_i18n.t(QStringLiteral("msg.error")),
                  m_i18n.t(QStringLiteral("msg.openFolderFailed")).arg(folder));
    }
}

void MainWindow::showInfo(const QString &title, const QString &text)
{
    QMessageBox box(QMessageBox::Information, title, text, QMessageBox::NoButton, this);
    box.addButton(m_i18n.t(QStringLiteral("msg.ok")), QMessageBox::AcceptRole);
    box.exec();
}

void MainWindow::showError(const QString &title, const QString &text)
{
    QMessageBox box(QMessageBox::Critical, title, text, QMessageBox::NoButton, this);
    box.addButton(m_i18n.t(QStringLiteral("msg.ok")), QMessageBox::AcceptRole);
    box.exec();
}

bool MainWindow::askDelete(const QString &name)
{
    QMessageBox box(QMessageBox::Question,
                    m_i18n.t(QStringLiteral("msg.deleteTitle")),
                    m_i18n.t(QStringLiteral("msg.deleteText")).arg(name),
                    QMessageBox::NoButton, this);
    QPushButton *yesButton = box.addButton(m_i18n.t(QStringLiteral("msg.yes")),
                                           QMessageBox::DestructiveRole);
    QPushButton *noButton = box.addButton(m_i18n.t(QStringLiteral("msg.no")),
                                          QMessageBox::RejectRole);
    box.setDefaultButton(noButton);
    box.setEscapeButton(noButton);
    box.exec();
    return box.clickedButton() == yesButton;
}

bool MainWindow::exportRecords(const QVector<EntryRecord> &records,
                               const QString &suggestedName)
{
    const QString directory = m_lastExportDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : m_lastExportDir;
    QString path = QFileDialog::getSaveFileName(
        this, m_i18n.t(QStringLiteral("msg.exportTitle")),
        QDir(directory).filePath(suggestedName + QStringLiteral(".json")),
        m_i18n.t(QStringLiteral("filter.json")));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        path += QStringLiteral(".json");

    QJsonArray array;
    for (const EntryRecord &entry : records)
        array.append(entryToJson(entry));

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("ck3-dna-manager-qt"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("exportedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("entries"), array);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        showError(m_i18n.t(QStringLiteral("msg.exportTitle")),
                  m_i18n.t(QStringLiteral("msg.exportFailed")).arg(path));
        return false;
    }

    m_lastExportDir = QFileInfo(path).absolutePath();
    QSettings settings;
    settings.setValue(QStringLiteral("dirs/export"), m_lastExportDir);
    statusBar()->showMessage(m_i18n.t(QStringLiteral("msg.exported")), 4000);
    return true;
}

QJsonObject MainWindow::entryToJson(const EntryRecord &entry) const
{
    DnaParseResult parsed = DnaCodec::fromBytes(
        entry.dna, Base64Hint::Modern, m_catalog, m_legacyCatalog);
    if (parsed.ok && !entry.rulerMeta.isEmpty())
        DnaCodec::applyRulerMetadata(parsed.data, entry.rulerMeta);

    const int geneCount = static_cast<int>(entry.dna.size() / 4);
    QJsonObject object;
    object.insert(QStringLiteral("name"), entry.name);
    object.insert(QStringLiteral("notes"), entry.notes);
    object.insert(QStringLiteral("favorite"), entry.favorite);
    if (!entry.rulerMeta.isEmpty())
        object.insert(QStringLiteral("rulerMeta"), entry.rulerMeta);
    object.insert(QStringLiteral("base64"), QString::fromLatin1(entry.dna.toBase64()));
    object.insert(QStringLiteral("geneCount"), geneCount);
    object.insert(QStringLiteral("moddedGeneCount"),
                  std::max(0, geneCount - m_catalog.base64GeneCount()));
    if (parsed.ok)
        object.insert(QStringLiteral("rulerDesigner"),
                      DnaCodec::toRuler(parsed.data, m_catalog));
    object.insert(QStringLiteral("createdAt"), static_cast<double>(entry.createdAt));
    object.insert(QStringLiteral("updatedAt"), static_cast<double>(entry.updatedAt));
    if (!entry.portrait.isEmpty()) {
        object.insert(QStringLiteral("portraitPng"),
                      QString::fromLatin1(entry.portrait.toBase64()));
    }
    return object;
}

QString MainWindow::dataDirectory() const
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    // QStandardPaths nests <application> inside <organization>. Both use the
    // product name here, so take the parent to keep a single, clean folder:
    // Windows: %APPDATA%\CK3 DNA Manager Qt
    // Linux:   ~/.local/share/CK3 DNA Manager Qt
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString directory = appData.isEmpty() ? QString() : QFileInfo(appData).dir().absolutePath();
#else
    QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
    if (directory.isEmpty())
        directory = QDir::home().filePath(QStringLiteral(".ck3-dna-manager-qt"));
    QDir().mkpath(directory);
    return directory;
}

QString MainWindow::dateText(qint64 milliseconds) const
{
    if (milliseconds <= 0)
        return QStringLiteral("—");
    QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(milliseconds);
    dateTime = dateTime.toLocalTime();
    return m_i18n.locale().toString(dateTime, QLocale::ShortFormat);
}

QString MainWindow::subtitleFor(const EntryRecord &entry) const
{
    const QString notes = entry.notes.trimmed();
    if (!notes.isEmpty()) {
        const QString firstLine = notes.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (!firstLine.isEmpty())
            return firstLine;
    }

    const int genes = entry.dna.size() / 4;
    const int extras = std::max(0, genes - m_catalog.base64GeneCount());
    QString text = m_i18n.t(QStringLiteral("detail.genesCount")).arg(genes);
    if (extras > 0)
        text += QStringLiteral(" · ")
            + m_i18n.t(QStringLiteral("detail.moddedCount")).arg(extras);
    return text;
}

QString MainWindow::defaultDialogDirectory(const QString &kind) const
{
    const QString stored = kind == QStringLiteral("import") ? m_lastImportDir : m_lastExportDir;
    if (!stored.isEmpty() && QFileInfo::exists(stored))
        return stored;
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

QString MainWindow::sanitizedFileName(const QString &name) const
{
    QString result = name.trimmed();
    static const QRegularExpression invalid(QStringLiteral("[\\\\/:*?\"<>|]"));
    result.replace(invalid, QStringLiteral("_"));
    result = result.simplified();
    if (result.isEmpty())
        result = QStringLiteral("dna");
    return result;
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Paste) && m_hasCurrent
        && ImageUtils::hasClipboardImage()) {
        pastePortrait();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->matches(QKeySequence::Paste)
            && ImageUtils::hasClipboardImage()
            && !ImageUtils::hasClipboardText()) {
            pastePortrait();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSaveBeforeLeave()) {
        event->ignore();
        return;
    }
    saveSettings();
    event->accept();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    scheduleInitialLoad();
}

void MainWindow::scheduleInitialLoad()
{
    if (m_initialLoadScheduled)
        return;
    m_initialLoadScheduled = true;
    QTimer::singleShot(0, this, [this] {
        if (!m_initialLoaded)
            refreshAll();
    });
}

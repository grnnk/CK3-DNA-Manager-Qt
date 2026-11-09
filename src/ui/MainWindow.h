#pragma once

#include "core/Database.h"
#include "core/DnaData.h"
#include "core/GeneCatalog.h"
#include "core/I18n.h"

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QMainWindow>
#include <QVector>

class QAction;
class QActionGroup;
class QComboBox;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QTextEdit;
class QToolButton;
class PortraitWidget;
class GeneEditorWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool initialize(QString *error = nullptr);
    void refreshAll(const QString &selectId = QString());
    void scheduleInitialLoad();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class EntrySort { Updated, Created, Name };

    void setupUi();
    void setupMenus();
    void applyTheme();
    void loadSettings();
    void saveSettings();
    void retranslateUi();

    void reloadEntryList(const QString &selectId);
    void rebuildList(const QString &selectId);
    void sortEntries();
    void applyListFilter();
    bool selectRowById(const QString &id);
    void loadEntry(const QString &id);
    void clearEntry();
    void updateEntryView();
    void updateCodes();
    void updateMeta();
    void updateActionStates();
    void updateHeader();
    void updateFavoriteButton();
    void updateSaveButton();
    void updatePortraitButtons();
    void updateCountLabel();

    bool saveEntry(bool showMessage);
    bool maybeSaveBeforeLeave();
    void changeLanguage(const QString &code);

    void newEntry();
    void newEntryWithImage(const QImage &image);
    void quickConvert();
    void importLegacy();
    void exportCurrent();
    void exportAll();
    void duplicateCurrent();
    void deleteCurrent();
    void copyBase64();
    void copyRuler();
    void copyCurrentCodeTab();
    void choosePortrait();
    void pastePortrait();
    void removePortrait();
    void applyPortrait(const QImage &image);
    void toggleFavorite(bool favorite);
    void showAbout();
    void openDataFolder();

    void showInfo(const QString &title, const QString &text);
    void showError(const QString &title, const QString &text);
    bool askDelete(const QString &name);
    bool exportRecords(const QVector<EntryRecord> &records, const QString &suggestedName);
    QJsonObject entryToJson(const EntryRecord &entry) const;

    QString dataDirectory() const;
    QString dateText(qint64 milliseconds) const;
    QString subtitleFor(const EntryRecord &entry) const;
    QString defaultDialogDirectory(const QString &kind) const;
    QString sanitizedFileName(const QString &name) const;

    I18n m_i18n;
    GeneCatalog m_catalog;
    GeneCatalog m_legacyCatalog;
    Database m_database;

    QVector<EntryRecord> m_entries;
    EntryRecord m_current;
    DnaData m_currentData;
    EntrySort m_sort = EntrySort::Updated;
    QString m_lastImportDir;
    QString m_lastExportDir;

    bool m_hasCurrent = false;
    bool m_currentDnaValid = false;
    bool m_detailsDirty = false;
    bool m_loading = false;
    bool m_initialLoaded = false;
    bool m_initialLoadScheduled = false;

    QLabel *m_brandLabel = nullptr;
    QLabel *m_libraryTitle = nullptr;
    QLabel *m_countLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_sortLabel = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QListWidget *m_list = nullptr;
    class EntryListDelegate *m_listDelegate = nullptr;
    QStackedWidget *m_listStack = nullptr;
    QLabel *m_listEmptyTitle = nullptr;
    QLabel *m_listEmptyHint = nullptr;
    QPushButton *m_newButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_quickConvertButton = nullptr;
    QLabel *m_languageLabel = nullptr;
    QComboBox *m_languageCombo = nullptr;

    QStackedWidget *m_mainStack = nullptr;
    QWidget *m_welcomePage = nullptr;
    QWidget *m_contentPage = nullptr;
    QLabel *m_welcomeTitle = nullptr;
    QLabel *m_welcomeHint = nullptr;
    QPushButton *m_welcomeNewButton = nullptr;
    QPushButton *m_welcomeImportButton = nullptr;

    QLabel *m_detailTitle = nullptr;
    QToolButton *m_favoriteButton = nullptr;
    QTabWidget *m_tabs = nullptr;

    PortraitWidget *m_portrait = nullptr;
    QPushButton *m_changePortraitButton = nullptr;
    QPushButton *m_pastePortraitButton = nullptr;
    QPushButton *m_removePortraitButton = nullptr;
    QLabel *m_nameLabel = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_notesLabel = nullptr;
    QTextEdit *m_notesEdit = nullptr;
    QLabel *m_metaLabel = nullptr;
    QPushButton *m_editGenesButton = nullptr;
    QPushButton *m_copyBase64Button = nullptr;
    QPushButton *m_copyRulerButton = nullptr;
    QPushButton *m_saveDetailsButton = nullptr;
    QPushButton *m_duplicateButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_deleteButton = nullptr;

    QTabWidget *m_codeTabs = nullptr;
    QPlainTextEdit *m_base64Edit = nullptr;
    QPlainTextEdit *m_rulerEdit = nullptr;
    QPushButton *m_copyCodeButton = nullptr;

    GeneEditorWidget *m_geneEditor = nullptr;

    QMenu *m_fileMenu = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_helpMenu = nullptr;
    QMenu *m_languageMenu = nullptr;
    QActionGroup *m_languageGroup = nullptr;
    QHash<QString, QAction *> m_languageActions;

    QAction *m_newAction = nullptr;
    QAction *m_quickConvertAction = nullptr;
    QAction *m_importLegacyAction = nullptr;
    QAction *m_exportAction = nullptr;
    QAction *m_exportAllAction = nullptr;
    QAction *m_openDataFolderAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_copyBase64Action = nullptr;
    QAction *m_copyRulerAction = nullptr;
    QAction *m_duplicateAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_findAction = nullptr;
    QAction *m_aboutAction = nullptr;
};

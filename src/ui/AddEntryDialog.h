#pragma once

#include "core/Database.h"
#include "core/DnaCodec.h"

#include <QDialog>
#include <QImage>

class I18n;
class GeneCatalog;
class QComboBox;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class PortraitWidget;

class AddEntryDialog : public QDialog
{
    Q_OBJECT

public:
    AddEntryDialog(const GeneCatalog &catalog,
                   const GeneCatalog &legacyCatalog,
                   I18n &i18n,
                   QWidget *parent = nullptr,
                   const QImage &initialImage = QImage());

    const EntryRecord &record() const { return m_record; }
    const DnaParseResult &parseResult() const { return m_parseResult; }

public slots:
    void accept() override;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void retranslateUi();
    void validateInput();
    void updateCreateButton();
    void updatePasteButton();
    void setStatus(const QString &text, const QString &state);
    void chooseImage();
    void pasteImage();
    void removeImage();
    Base64Hint base64Hint() const;

    const GeneCatalog &m_catalog;
    const GeneCatalog &m_legacyCatalog;
    I18n &m_i18n;

    QLineEdit *m_nameEdit = nullptr;
    QPlainTextEdit *m_dnaEdit = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    PortraitWidget *m_portrait = nullptr;
    QPushButton *m_chooseImageButton = nullptr;
    QPushButton *m_pasteImageButton = nullptr;
    QPushButton *m_removeImageButton = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QTimer *m_validationTimer = nullptr;

    QLabel *m_nameLabel = nullptr;
    QLabel *m_dnaLabel = nullptr;
    QLabel *m_formatLabel = nullptr;
    QLabel *m_portraitLabel = nullptr;

    QImage m_image;
    DnaParseResult m_parseResult;
    EntryRecord m_record;
    bool m_parsedOk = false;
};

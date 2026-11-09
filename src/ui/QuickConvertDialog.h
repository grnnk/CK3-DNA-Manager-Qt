#pragma once

#include "core/DnaCodec.h"

#include <QDialog>

class GeneCatalog;
class I18n;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;

/// Lightweight Base64 <-> Ruler Designer converter. It never creates or
/// modifies a library entry.
class QuickConvertDialog : public QDialog
{
    Q_OBJECT

public:
    QuickConvertDialog(const GeneCatalog &catalog,
                       const GeneCatalog &legacyCatalog,
                       I18n &i18n,
                       QWidget *parent = nullptr);

private:
    void buildUi();
    void retranslateUi();
    void validateInput();
    void setStatus(const QString &text, const QString &state);
    void copyBase64();
    void copyRuler();
    Base64Hint base64Hint() const;

    const GeneCatalog &m_catalog;
    const GeneCatalog &m_legacyCatalog;
    I18n &m_i18n;

    QLabel *m_inputLabel = nullptr;
    QLabel *m_formatLabel = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QPlainTextEdit *m_inputEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_outputLabel = nullptr;
    QLabel *m_base64Label = nullptr;
    QLabel *m_rulerLabel = nullptr;
    QPlainTextEdit *m_base64Output = nullptr;
    QPlainTextEdit *m_rulerOutput = nullptr;
    QPushButton *m_copyBase64Button = nullptr;
    QPushButton *m_copyRulerButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QTimer *m_validationTimer = nullptr;
};

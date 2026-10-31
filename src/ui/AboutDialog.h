#pragma once

#include <QDialog>

class I18n;
class QLabel;
class QPushButton;

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(I18n &i18n, QWidget *parent = nullptr);

private:
    I18n &m_i18n;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_versionLabel = nullptr;
    QLabel *m_descriptionLabel = nullptr;
    QLabel *m_creditsLabel = nullptr;
    QPushButton *m_closeButton = nullptr;
};

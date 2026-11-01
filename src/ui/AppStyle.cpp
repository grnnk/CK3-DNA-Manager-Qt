#include "ui/AppStyle.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

void applyAppStyle(QApplication &application)
{
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#14110d")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#e8dfcf")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#1d1812")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#191510")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#ece3d2")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#241e17")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e6dcc9")));
    palette.setColor(QPalette::BrightText, QColor(QStringLiteral("#f0c94e")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#6f5a1f")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#f7f0df")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#241e17")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#e8dfcf")));
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#7f7464")));
    palette.setColor(QPalette::Link, QColor(QStringLiteral("#d8b84a")));
    palette.setColor(QPalette::LinkVisited, QColor(QStringLiteral("#b99a3e")));
    palette.setColor(QPalette::Disabled, QPalette::WindowText,
                     QColor(QStringLiteral("#6c6152")));
    palette.setColor(QPalette::Disabled, QPalette::Text,
                     QColor(QStringLiteral("#6c6152")));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     QColor(QStringLiteral("#6c6152")));
    application.setPalette(palette);

#ifdef Q_OS_WIN
    QFont font(QStringLiteral("Segoe UI"));
#else
    // Use the platform's default UI font on Linux and macOS instead of a font
    // that may not be installed.
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
#endif
    font.setPointSizeF(9.5);
    font.setStyleStrategy(QFont::PreferAntialias);
    application.setFont(font);
}

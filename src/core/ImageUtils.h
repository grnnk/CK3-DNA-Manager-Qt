#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>

namespace ImageUtils {

QImage loadImage(const QString &path, QString *error = nullptr);
QImage imageFromBytes(const QByteArray &bytes);
bool isSupportedImagePath(const QString &path);
QStringList supportedImageSuffixes();
bool hasClipboardImage();
bool hasClipboardText();
QImage imageFromClipboard();
QByteArray encodePng(const QImage &image);
QByteArray makeThumbnail(const QImage &image, const QSize &target = QSize(96, 120));
QSize thumbnailSize();

} // namespace ImageUtils

#include "ui/PortraitWidget.h"

#include "core/ImageUtils.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

namespace {

bool hasImageUrl(const QMimeData *mimeData)
{
    if (!mimeData->hasUrls())
        return false;
    for (const QUrl &url : mimeData->urls()) {
        if (url.isLocalFile() && ImageUtils::isSupportedImagePath(url.toLocalFile()))
            return true;
    }
    return false;
}

} // namespace

PortraitWidget::PortraitWidget(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PortraitWidget::setImage(const QImage &image)
{
    m_image = image;
    update();
}

void PortraitWidget::clearImage()
{
    m_image = QImage();
    update();
}

void PortraitWidget::setPlaceholderText(const QString &text)
{
    m_placeholderText = text;
    update();
}

void PortraitWidget::setInteractive(bool interactive)
{
    m_interactive = interactive;
    setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setAcceptDrops(interactive);
    update();
}

QSize PortraitWidget::sizeHint() const
{
    return QSize(300, 400);
}

QSize PortraitWidget::minimumSizeHint() const
{
    return QSize(220, 280);
}

void PortraitWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF frame = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    QPainterPath framePath;
    framePath.addRoundedRect(frame, 10, 10);

    painter.setClipPath(framePath);
    painter.fillRect(frame, QColor(QStringLiteral("#100d0a")));

    if (!m_image.isNull()) {
        const QSizeF scaled = QSizeF(m_image.size()).scaled(frame.size(),
                                                             Qt::KeepAspectRatio);
        const QRectF target(frame.center().x() - scaled.width() / 2.0,
                            frame.center().y() - scaled.height() / 2.0,
                            scaled.width(), scaled.height());
        painter.drawImage(target, m_image);
    } else {
        painter.setPen(QPen(QColor(m_dragActive ? QStringLiteral("#d8b84a")
                                                 : QStringLiteral("#3b3227")),
                            1.6, Qt::DashLine));
        painter.drawRoundedRect(frame.adjusted(8, 8, -8, -8), 8, 8);

        QFont titleFont = font();
        titleFont.setPointSizeF(titleFont.pointSizeF() + 1.0);
        titleFont.setBold(true);
        painter.setFont(titleFont);
        painter.setPen(QColor(QStringLiteral("#b6a98f")));
        painter.drawText(frame.adjusted(24, 24, -24, -52),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         m_placeholderText);

        painter.setPen(QColor(QStringLiteral("#6d6252")));
        QFont smallFont = font();
        smallFont.setPointSizeF(smallFont.pointSizeF() - 1.0);
        painter.setFont(smallFont);
        painter.drawText(frame.adjusted(24, 0, -24, 24),
                         Qt::AlignHCenter | Qt::AlignBottom,
                         QStringLiteral("PNG · JPG · WEBP · BMP · GIF"));
    }

    painter.setClipping(false);
    painter.setPen(QPen(QColor(QStringLiteral("#3a3025")), 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(frame, 10, 10);
}

void PortraitWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_interactive && event->button() == Qt::LeftButton)
        emit chooseRequested();
    QWidget::mousePressEvent(event);
}

void PortraitWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_interactive && hasImageUrl(event->mimeData())) {
        m_dragActive = true;
        update();
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void PortraitWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (m_interactive && hasImageUrl(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void PortraitWidget::dropEvent(QDropEvent *event)
{
    m_dragActive = false;
    update();

    if (!m_interactive || !hasImageUrl(event->mimeData())) {
        event->ignore();
        return;
    }

    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        if (!ImageUtils::isSupportedImagePath(url.toLocalFile()))
            continue;
        const QImage image = ImageUtils::loadImage(url.toLocalFile());
        if (!image.isNull()) {
            event->acceptProposedAction();
            emit imageDropped(image);
            return;
        }
    }
    event->ignore();
}

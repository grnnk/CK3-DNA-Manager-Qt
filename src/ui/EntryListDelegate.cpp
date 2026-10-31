#include "ui/EntryListDelegate.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

void drawThumbnail(QPainter *painter, const QRectF &rect, const QPixmap &pixmap)
{
    QPainterPath path;
    path.addRoundedRect(rect, 5, 5);
    painter->save();
    painter->setClipPath(path);
    painter->fillRect(rect, QColor(QStringLiteral("#0f0d0a")));

    if (!pixmap.isNull()) {
        const QSizeF scaled = QSizeF(pixmap.size()).scaled(rect.size(),
                                                           Qt::KeepAspectRatioByExpanding);
        const QRectF target(rect.center().x() - scaled.width() / 2.0,
                            rect.center().y() - scaled.height() / 2.0,
                            scaled.width(), scaled.height());
        painter->drawPixmap(target, pixmap, QRectF(pixmap.rect()));
    } else {
        QFont font = painter->font();
        font.setPointSizeF(7.5);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(QColor(QStringLiteral("#5b5040")));
        painter->drawText(rect, Qt::AlignCenter, QStringLiteral("DNA"));
    }
    painter->restore();

    // The caller may have left a fill brush active for the row background;
    // clear it so this frame stays an outline and does not cover the image.
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(QStringLiteral("#3a3025")), 1));
    painter->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
}

} // namespace

EntryListDelegate::EntryListDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void EntryListDelegate::clearCache(const QString &id)
{
    if (id.isEmpty())
        m_thumbnailCache.clear();
    else
        m_thumbnailCache.remove(id);
}

void EntryListDelegate::paint(QPainter *painter,
                              const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    const QRectF background = QRectF(option.rect).adjusted(5, 3, -5, -3);
    if (selected) {
        painter->setPen(QPen(QColor(QStringLiteral("#7d6726")), 1));
        painter->setBrush(QColor(QStringLiteral("#2c251a")));
        painter->drawRoundedRect(background, 8, 8);
        painter->fillRect(QRectF(background.left() + 1, background.top() + 10, 3,
                                 background.height() - 20),
                          QColor(QStringLiteral("#d8b84a")));
    } else if (hovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(QStringLiteral("#221d16")));
        painter->drawRoundedRect(background, 8, 8);
    }

    const QRectF thumbnailRect(background.left() + 10, background.top() + 10, 48, 60);
    QPixmap pixmap = index.data(EntryRoles::Thumbnail).value<QPixmap>();
    if (pixmap.isNull()) {
        const QString entryId = index.data(EntryRoles::EntryId).toString();
        auto it = m_thumbnailCache.constFind(entryId);
        if (it != m_thumbnailCache.constEnd()) {
            pixmap = it.value();
        } else {
            const QByteArray bytes = index.data(EntryRoles::ThumbnailBytes).toByteArray();
            if (!bytes.isEmpty()) {
                if (pixmap.loadFromData(bytes, "PNG")) {
                    m_thumbnailCache.insert(entryId, pixmap);
                }
            }
        }
    }
    drawThumbnail(painter, thumbnailRect, pixmap);

    const qreal textLeft = thumbnailRect.right() + 12;
    const qreal textRight = background.right() - 13;
    const qreal textWidth = textRight - textLeft;

    QFont nameFont = option.font;
    nameFont.setBold(true);
    nameFont.setPointSizeF(nameFont.pointSizeF() + 0.4);
    painter->setFont(nameFont);
    painter->setPen(selected ? QColor(QStringLiteral("#f4ead3"))
                             : QColor(QStringLiteral("#e8dfcf")));
    const QFontMetrics nameMetrics(nameFont);
    const QString name = nameMetrics.elidedText(index.data(EntryRoles::Name).toString(),
                                                Qt::ElideRight, static_cast<int>(textWidth));
    painter->drawText(QRectF(textLeft, background.top() + 9, textWidth, 21),
                      Qt::AlignLeft | Qt::AlignVCenter, name);

    QFont smallFont = option.font;
    smallFont.setPointSizeF(smallFont.pointSizeF() - 1.0);
    painter->setFont(smallFont);
    painter->setPen(QColor(QStringLiteral("#a99d88")));
    const QFontMetrics smallMetrics(smallFont);

    QString subtitle = index.data(EntryRoles::Subtitle).toString();
    if (subtitle.isEmpty())
        subtitle = index.data(EntryRoles::DateText).toString();
    subtitle = smallMetrics.elidedText(subtitle, Qt::ElideRight,
                                       static_cast<int>(textWidth - 18));
    painter->drawText(QRectF(textLeft, background.top() + 31, textWidth - 18, 18),
                      Qt::AlignLeft | Qt::AlignVCenter, subtitle);

    const QString dateText = index.data(EntryRoles::DateText).toString();
    painter->setPen(QColor(QStringLiteral("#7f7464")));
    painter->drawText(QRectF(textLeft, background.bottom() - 22, textWidth, 16),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      smallMetrics.elidedText(dateText, Qt::ElideRight,
                                              static_cast<int>(textWidth)));

    if (index.data(EntryRoles::Favorite).toBool()) {
        QFont starFont = option.font;
        starFont.setPointSizeF(starFont.pointSizeF() + 1.5);
        painter->setFont(starFont);
        painter->setPen(QColor(QStringLiteral("#d8b84a")));
        painter->drawText(QRectF(background.right() - 26, background.top() + 6, 20, 20),
                          Qt::AlignCenter, QStringLiteral("★"));
    }

    painter->restore();
}

QSize EntryListDelegate::sizeHint(const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(280, 82);
}

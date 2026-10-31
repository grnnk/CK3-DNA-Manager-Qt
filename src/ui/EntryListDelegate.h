#pragma once

#include <QHash>
#include <QPixmap>
#include <QStyledItemDelegate>

namespace EntryRoles {
enum Role {
    EntryId = Qt::UserRole + 1,
    Name,
    Subtitle,
    DateText,
    Favorite,
    Thumbnail,
    SearchText,
    ThumbnailBytes,
};
}

class EntryListDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit EntryListDelegate(QObject *parent = nullptr);

    void clearCache(const QString &id = QString());

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

private:
    mutable QHash<QString, QPixmap> m_thumbnailCache;
};

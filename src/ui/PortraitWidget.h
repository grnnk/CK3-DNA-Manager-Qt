#pragma once

#include <QImage>
#include <QWidget>

class PortraitWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PortraitWidget(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void clearImage();
    bool hasImage() const { return !m_image.isNull(); }
    QImage image() const { return m_image; }

    void setPlaceholderText(const QString &text);
    void setInteractive(bool interactive);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void chooseRequested();
    void imageDropped(const QImage &image);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QImage m_image;
    QString m_placeholderText;
    bool m_interactive = true;
    bool m_dragActive = false;
};

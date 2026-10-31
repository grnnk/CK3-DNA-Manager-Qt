#pragma once

#include <QLayout>
#include <QList>

/// A layout that arranges widgets left to right and wraps them onto new lines
/// when the available width is exhausted. Used for the detail action buttons so
/// longer translations and narrow windows do not clip the panel.
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent = nullptr, int margin = -1,
                        int horizontalSpacing = -1, int verticalSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QLayoutItem *takeAt(int index) override;

    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize minimumSize() const override;
    QSize sizeHint() const override;
    void setGeometry(const QRect &rect) override;

    int horizontalSpacing() const;
    int verticalSpacing() const;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    int smartSpacing(int pixelMetric) const;

    QList<QLayoutItem *> m_items;
    int m_horizontalSpacing;
    int m_verticalSpacing;
};

#pragma once

#include <QLayout>
#include <QList>

namespace colorizing::app {

// Lays its items out in a row and starts a new row where the next one does not fit, like
// words in a paragraph. The last item can take what is left of its row (the toolbar's status
// text); when less than its minimum width is left, it starts a row of its own.
class FlowLayout final : public QLayout {
public:
    explicit FlowLayout(QWidget* parent, int horizontalSpacing, int verticalSpacing);
    ~FlowLayout() override;

    // The last item fills the rest of its row; minimumWidth is the least it is given.
    void setStretchLastItem(bool stretch, int minimumWidth = 160);

    void addItem(QLayoutItem* item) override;
    [[nodiscard]] int count() const override;
    [[nodiscard]] QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    [[nodiscard]] Qt::Orientations expandingDirections() const override;
    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;
    [[nodiscard]] QSize minimumSize() const override;
    [[nodiscard]] QSize sizeHint() const override;
    void setGeometry(const QRect& rect) override;

private:
    // Places the items (unless testOnly) and returns the height they need.
    int arrange(const QRect& rect, bool testOnly) const;

    QList<QLayoutItem*> m_items;
    int m_horizontalSpacing;
    int m_verticalSpacing;
    bool m_stretchLast = false;
    int m_lastMinimumWidth = 160;
};

}  // namespace colorizing::app

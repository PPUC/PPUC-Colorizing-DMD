#include "DrawingToolIcons.h"

#include "Appearance.h"

#include <QApplication>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QString>

namespace colorizing::app {

namespace {

// A black picture on transparent (third-party/phosphor-icons) in the color of the palette.
class ToolIconEngine final : public QIconEngine {
public:
    explicit ToolIconEngine(QString resource) : m_resource(std::move(resource)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal ratio = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        QImage image = QImage(m_resource)
                           .scaled(rect.size() * ratio, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QPalette palette = QApplication::palette();
        const QColor color = mode == QIcon::Disabled
                                 ? palette.color(QPalette::Disabled, QPalette::ButtonText)
                             : state == QIcon::On ? checkedButtonTextColor(palette)
                                                  : palette.color(QPalette::ButtonText);
        QPainter tint(&image);
        tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tint.fillRect(image.rect(), color);
        tint.end();
        image.setDevicePixelRatio(ratio);
        painter->drawImage(rect, image);
    }

    // QIconEngine's own leaves what the picture does not cover uninitialized.
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

    [[nodiscard]] QIconEngine* clone() const override { return new ToolIconEngine(m_resource); }

private:
    QString m_resource;
};

QString iconName(edit::DrawingTool tool)
{
    using edit::DrawingTool;
    switch (tool) {
        case DrawingTool::Point:
            return QStringLiteral("pencil-simple");
        case DrawingTool::Line:
            return QStringLiteral("line-segment");
        case DrawingTool::Rectangle:
            return QStringLiteral("rectangle");
        case DrawingTool::FilledRectangle:
            return QStringLiteral("rectangle-fill");
        case DrawingTool::Circle:
            return QStringLiteral("circle");
        case DrawingTool::FilledCircle:
            return QStringLiteral("circle-fill");
        case DrawingTool::Ellipse:
            return QStringLiteral("ellipse");
        case DrawingTool::FilledEllipse:
            return QStringLiteral("ellipse-fill");
        case DrawingTool::PaletteGradient:
            return QStringLiteral("gradient");
        case DrawingTool::Selection:
            return QStringLiteral("selection");
        case DrawingTool::ColorPicker:
            return QStringLiteral("eyedropper");
        case DrawingTool::FloodFill:
            return QStringLiteral("paint-bucket");
    }
    return QString();
}

}  // namespace

QIcon drawingToolIcon(edit::DrawingTool tool)
{
    return QIcon(new ToolIconEngine(QStringLiteral(":/tool-icons/%1.png").arg(iconName(tool))));
}

}  // namespace colorizing::app

#include "DynamicZoneMode.h"

#include <QSettings>
#include <QString>

namespace colorizing::app {

namespace {

const QString kLeftPaintsColorsKey = QStringLiteral("canvas/dynamicLeftButtonPaintsColors");
const QString kOnOriginalKey = QStringLiteral("canvas/dynamicZonesOnOriginal");

}  // namespace

DynamicZoneOptions loadDynamicZoneOptions()
{
    const QSettings settings;
    DynamicZoneOptions options;
    options.leftPaintsColors =
        settings.value(kLeftPaintsColorsKey, options.leftPaintsColors).toBool();
    options.onOriginal = settings.value(kOnOriginalKey, options.onOriginal).toBool();
    return options;
}

void saveDynamicZoneOptions(const DynamicZoneOptions& options)
{
    QSettings settings;
    settings.setValue(kLeftPaintsColorsKey, options.leftPaintsColors);
    settings.setValue(kOnOriginalKey, options.onOriginal);
}

CanvasEditLayer canvasEditLayer(const CanvasModes& modes,
                                const DynamicZoneOptions& options,
                                CanvasStroke stroke,
                                bool onColorized)
{
    if (stroke == CanvasStroke::Right) {
        return CanvasEditLayer::DynamicMask;
    }
    if (modes.comparisonMask) {
        // Beside the zones the colorized picture paints colors; the mask is the original's.
        if (options.leftPaintsColors && stroke == CanvasStroke::Left && onColorized) {
            return modes.backgroundImage ? CanvasEditLayer::BackgroundImage
                                         : CanvasEditLayer::FramePixels;
        }
        return CanvasEditLayer::ComparisonMask;
    }
    if (modes.dynamicZones && !options.leftPaintsColors) {
        return CanvasEditLayer::DynamicMask;
    }
    if (modes.backgroundMask) {
        return CanvasEditLayer::BackgroundMask;
    }
    if (modes.backgroundImage) {
        return CanvasEditLayer::BackgroundImage;
    }
    return CanvasEditLayer::FramePixels;
}

bool maskWithDynamicZones(const DynamicZoneOptions& options)
{
    return options.leftPaintsColors;
}

bool dynamicZonesOnOriginal(const CanvasModes& modes, const DynamicZoneOptions& options)
{
    return options.onOriginal && !modes.comparisonMask;
}

bool paintsDynamicZonesWithRightButton(edit::DrawingTool tool)
{
    return tool != edit::DrawingTool::Selection && tool != edit::DrawingTool::PaletteGradient;
}

}  // namespace colorizing::app

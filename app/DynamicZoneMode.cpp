#include "DynamicZoneMode.h"

#include <array>
#include <utility>

#include <QSettings>
#include <QString>

namespace colorizing::app {

namespace {

const QString kLeftPaintsColorsKey = QStringLiteral("canvas/dynamicLeftButtonPaintsColors");
const QString kOnOriginalKey = QStringLiteral("canvas/dynamicZonesOnOriginal");

// The neighbours an area grows by: the four sides and the four corners.
constexpr std::array<std::pair<int, int>, 8> kAreaNeighbours{
    {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}}};

}  // namespace

std::vector<DynamicSetLabel> dynamicSetLabels(const std::vector<int>& sets,
                                              int width,
                                              int height)
{
    const auto index = [width](int x, int y) {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x);
    };
    std::vector<DynamicSetLabel> labels;
    std::vector<bool> seen(sets.size(), false);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int set = sets[index(x, y)];
            if (set == 255 || seen[index(x, y)]) {
                continue;
            }
            std::vector<std::pair<int, int>> pending{{x, y}};
            seen[index(x, y)] = true;
            while (!pending.empty()) {
                const auto [px, py] = pending.back();
                pending.pop_back();
                for (const auto& [dx, dy] : kAreaNeighbours) {
                    const int nx = px + dx;
                    const int ny = py + dy;
                    if (nx >= 0 && ny >= 0 && nx < width && ny < height &&
                        !seen[index(nx, ny)] && sets[index(nx, ny)] == set) {
                        seen[index(nx, ny)] = true;
                        pending.push_back({nx, ny});
                    }
                }
            }
            labels.push_back({render::Point{x, y}, set});
        }
    }
    return labels;
}

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

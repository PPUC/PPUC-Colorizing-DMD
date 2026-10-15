#include "CanvasEditController.h"

#include <algorithm>
#include <exception>
#include <set>
#include <tuple>

#include "FrameImageUtils.h"
#include "render/ProjectGeometry.h"

namespace colorizing::app {

namespace {

using edit::DrawingTool;
using edit::PaletteGradientDirection;

constexpr int kShortStatusMs = 2000;
constexpr int kStatusMs = 2500;
constexpr int kLongStatusMs = 3000;
constexpr int kErrorStatusMs = 4000;

QString strokeStatusText(DrawingTool tool)
{
    switch (tool) {
        case DrawingTool::Line:
            return QStringLiteral("Painted line");
        case DrawingTool::Selection:
            return QStringLiteral("Selected canvas area");
        case DrawingTool::Rectangle:
            return QStringLiteral("Painted rectangle");
        case DrawingTool::FilledRectangle:
            return QStringLiteral("Painted filled rectangle");
        case DrawingTool::Circle:
            return QStringLiteral("Painted circle");
        case DrawingTool::FilledCircle:
            return QStringLiteral("Painted filled circle");
        case DrawingTool::Ellipse:
            return QStringLiteral("Painted ellipse");
        case DrawingTool::FilledEllipse:
            return QStringLiteral("Painted filled ellipse");
        case DrawingTool::PaletteGradient:
            return QStringLiteral("Filled palette gradient");
        case DrawingTool::Point:
        case DrawingTool::ColorPicker:
        case DrawingTool::FloodFill:
            break;
    }
    return QString();
}

render::ProjectGeometry projectGeometry(const CanvasEditContext& context)
{
    return context.document ? context.document->geometry() : render::ProjectGeometry();
}

bool isOnOriginal(const render::ProjectGeometry& geometry, const render::CanvasHit& hit)
{
    return hit.region == render::CanvasRegion::OriginalFrame &&
           hit.resolution == geometry.nativePlane();
}

QString onFramesText(const QString& statusText, std::size_t frameCount)
{
    if (frameCount <= 1) {
        return statusText;
    }
    return QStringLiteral("%1 on %2 frame(s)").arg(statusText).arg(static_cast<int>(frameCount));
}

QColor translucentDrawColor(const CanvasEditContext& context, int alpha)
{
    QColor color = rgb565ToColor(context.drawColor);
    color.setAlpha(alpha);
    return color;
}

std::vector<render::Point> withLine(std::vector<render::Point> pixels,
                                    const render::CanvasHit& start,
                                    const render::CanvasHit& end)
{
    const std::vector<render::Point> linePixels =
        edit::makeLinePixels(render::Point{start.x, start.y}, render::Point{end.x, end.y});
    pixels.insert(pixels.end(), linePixels.begin(), linePixels.end());
    return pixels;
}

}  // namespace

CanvasEditController::CanvasEditController(CanvasEditOutputs outputs)
    : m_outputs(std::move(outputs))
{
}

void CanvasEditController::colorizedPixelClicked(const CanvasEditContext& context,
                                                 const render::CanvasHit& hit,
                                                 bool erase)
{
    const DrawingTool tool = context.tool;
    if (tool != DrawingTool::Selection && isSelectionDirectionModeActive()) {
        showSelection();
        return;
    }
    if (tool == DrawingTool::PaletteGradient) {
        if (context.gradientDirection == PaletteGradientDirection::Free &&
            isFreeGradientPending()) {
            showPendingFreeGradient(context);
        }
        return;
    }
    const render::Point point{hit.x, hit.y};
    switch (context.layer) {
        case CanvasEditLayer::ComparisonMask:
            return;
        case CanvasEditLayer::BackgroundMask:
            if (tool == DrawingTool::FloodFill) {
                floodFillBackgroundMask(context, hit, erase);
            } else if (tool == DrawingTool::Point && context.document) {
                commitFrameMaskPixels(context,
                                      edit::EditKind::BackgroundMask,
                                      hit.resolution,
                                      {point},
                                      erase ? uint8_t{0} : uint8_t{1},
                                      erase ? QStringLiteral("Erased background mask pixel")
                                            : QStringLiteral("Painted background mask pixel"));
            }
            return;
        case CanvasEditLayer::DynamicMask:
            if (tool == DrawingTool::ColorPicker) {
                pickDynamicSet(context, hit);
            } else if (tool == DrawingTool::FloodFill) {
                floodFillDynamicMask(context, hit, erase);
            } else if (tool == DrawingTool::Point && context.document) {
                commitFrameMaskPixels(context,
                                      edit::EditKind::DynamicMask,
                                      hit.resolution,
                                      {point},
                                      erase ? uint8_t{255} : context.dynamicSet,
                                      erase ? QStringLiteral("Erased dynamic mask pixel")
                                            : QStringLiteral("Painted dynamic mask pixel"));
            }
            return;
        case CanvasEditLayer::BackgroundImage:
            if (tool == DrawingTool::ColorPicker) {
                pickBackgroundImageColor(context, hit, erase);
            } else if (tool == DrawingTool::FloodFill) {
                floodFillBackgroundImage(context, hit);
            } else if (tool == DrawingTool::Point && context.document) {
                commitBackgroundImagePixels(context,
                                            hit.resolution,
                                            {point},
                                            QStringLiteral("Painted background pixel at %1,%2")
                                                .arg(hit.x)
                                                .arg(hit.y));
            }
            return;
        case CanvasEditLayer::FramePixels:
            break;
    }
    if (tool == DrawingTool::ColorPicker) {
        pickFrameColor(context, hit, erase);
    } else if (tool == DrawingTool::FloodFill) {
        floodFillFrame(context, hit);
    } else if (tool == DrawingTool::Point) {
        commitFramePixels(context,
                          hit.resolution,
                          {point},
                          QStringLiteral("Painted pixel at %1,%2").arg(hit.x).arg(hit.y));
    }
}

void CanvasEditController::originalPixelClicked(const CanvasEditContext& context,
                                                const render::CanvasHit& hit,
                                                bool erase)
{
    if (context.spriteCaptureSelection) {
        if (context.tool != DrawingTool::Point ||
            !isOnOriginal(projectGeometry(context), hit)) {
            return;
        }
        paintCaptureSelection(context, {render::Point{hit.x, hit.y}}, erase);
        return;
    }
    // Dynamic zones are on the original's plane: they are painted, filled and picked on the
    // original as on the colorized picture.
    if (context.layer == CanvasEditLayer::DynamicMask) {
        if (isOnOriginal(projectGeometry(context), hit)) {
            colorizedPixelClicked(context, hit, erase);
        }
        return;
    }
    paintComparisonMaskPixel(context, hit, erase);
}

void CanvasEditController::strokePreviewed(const CanvasEditContext& context,
                                           const render::CanvasHit& start,
                                           const render::CanvasHit& end,
                                           bool erase)
{
    const DrawingTool tool = context.tool;
    if (tool == DrawingTool::Point || tool == DrawingTool::FloodFill ||
        start.region != end.region || start.resolution != end.resolution) {
        return;
    }
    const bool onColorized = start.region == render::CanvasRegion::ColorizedFrame;
    const bool onOriginal = start.region == render::CanvasRegion::OriginalFrame;
    const render::ProjectGeometry geometry = projectGeometry(context);
    const QColor eraseColor(0, 0, 0, 150);
    if (tool == DrawingTool::Selection) {
        if (onColorized) {
            showPreview(render::CanvasRegion::ColorizedFrame,
                        start.resolution,
                        strokePixels(context, start, end),
                        QColor(0, 220, 180, 130));
        }
        return;
    }
    if (isSelectionDirectionModeActive()) {
        previewSelectionDirection(start, end);
        return;
    }
    if (context.spriteCaptureSelection) {
        if (isOnOriginal(geometry, start)) {
            showPreview(render::CanvasRegion::OriginalFrame,
                        geometry.nativePlane(),
                        strokePixels(context, start, end),
                        erase ? eraseColor : QColor(255, 0, 255, 150));
        }
        return;
    }
    switch (context.layer) {
        case CanvasEditLayer::ComparisonMask:
            if (onOriginal) {
                showPreview(render::CanvasRegion::OriginalFrame,
                            geometry.nativePlane(),
                            strokePixels(context, start, end),
                            erase ? eraseColor : QColor(255, 0, 0, 150));
            }
            return;
        case CanvasEditLayer::DynamicMask:
            if (onColorized || isOnOriginal(geometry, start)) {
                showPreview(start.region,
                            start.resolution,
                            strokePixels(context, start, end),
                            erase ? eraseColor : QColor(0, 180, 255, 150));
            }
            return;
        case CanvasEditLayer::BackgroundMask:
            if (onColorized) {
                showPreview(render::CanvasRegion::ColorizedFrame,
                            start.resolution,
                            strokePixels(context, start, end),
                            erase ? eraseColor : QColor(255, 200, 0, 150));
            }
            return;
        case CanvasEditLayer::BackgroundImage:
        case CanvasEditLayer::FramePixels:
            break;
    }
    if (!onColorized) {
        return;
    }
    if (tool == DrawingTool::PaletteGradient &&
        context.gradientDirection == PaletteGradientDirection::Free && isFreeGradientPending()) {
        previewFreeGradientDirection(context, start, end);
        return;
    }
    showPreview(render::CanvasRegion::ColorizedFrame,
                start.resolution,
                strokePixels(context, start, end),
                translucentDrawColor(context, 150));
}

void CanvasEditController::strokeFinished(const CanvasEditContext& context,
                                          const render::CanvasHit& start,
                                          const render::CanvasHit& end,
                                          bool erase)
{
    const DrawingTool tool = context.tool;
    if (tool == DrawingTool::Point || tool == DrawingTool::FloodFill ||
        start.region != end.region || start.resolution != end.resolution) {
        return;
    }

    QString statusText = strokeStatusText(tool);
    if (tool != DrawingTool::Selection && isSelectionDirectionModeActive()) {
        setSelectionDirection(start, end);
        return;
    }

    const bool onColorized = start.region == render::CanvasRegion::ColorizedFrame;
    if (tool == DrawingTool::Selection) {
        // Selects in every edit mode, as its preview shows; it never paints a mask.
        if (onColorized) {
            setSelectionFromStroke(context, start, end);
        }
        return;
    }
    if (context.spriteCaptureSelection) {
        if (!isOnOriginal(projectGeometry(context), start)) {
            return;
        }
        paintCaptureSelection(context, strokePixels(context, start, end), erase);
        return;
    }
    const QString painted = QStringLiteral("Painted");
    const QString rgbOnly = QStringLiteral("Palette gradients edit RGB pixels only");
    switch (context.layer) {
        case CanvasEditLayer::ComparisonMask:
            if (start.region != render::CanvasRegion::OriginalFrame) {
                return;
            }
            if (tool == DrawingTool::PaletteGradient) {
                showStatus(rgbOnly, kStatusMs);
                return;
            }
            commitComparisonMaskPixels(
                context,
                strokePixels(context, start, end),
                erase ? uint8_t{0} : uint8_t{1},
                statusText.replace(painted,
                                   erase ? QStringLiteral("Erased comparison mask")
                                         : QStringLiteral("Painted comparison mask")));
            return;
        case CanvasEditLayer::DynamicMask:
            if (!onColorized && !isOnOriginal(projectGeometry(context), start)) {
                return;
            }
            if (tool == DrawingTool::PaletteGradient) {
                showStatus(rgbOnly, kStatusMs);
                return;
            }
            commitFrameMaskPixels(
                context,
                edit::EditKind::DynamicMask,
                start.resolution,
                strokePixels(context, start, end),
                erase ? uint8_t{255} : context.dynamicSet,
                statusText.replace(painted,
                                   erase ? QStringLiteral("Erased dynamic mask")
                                         : QStringLiteral("Painted dynamic mask")));
            return;
        case CanvasEditLayer::BackgroundMask:
            if (!onColorized) {
                return;
            }
            if (tool == DrawingTool::PaletteGradient) {
                showStatus(rgbOnly, kStatusMs);
                return;
            }
            commitFrameMaskPixels(
                context,
                edit::EditKind::BackgroundMask,
                start.resolution,
                strokePixels(context, start, end),
                erase ? uint8_t{0} : uint8_t{1},
                statusText.replace(painted,
                                   erase ? QStringLiteral("Erased background mask")
                                         : QStringLiteral("Painted background mask")));
            return;
        case CanvasEditLayer::BackgroundImage:
        case CanvasEditLayer::FramePixels:
            break;
    }

    if (!onColorized) {
        return;
    }
    const bool onBackground = context.layer == CanvasEditLayer::BackgroundImage;
    if (tool != DrawingTool::PaletteGradient) {
        if (onBackground) {
            commitBackgroundImagePixels(
                context,
                start.resolution,
                strokePixels(context, start, end),
                statusText.replace(painted, QStringLiteral("Painted background")));
        } else {
            commitFramePixels(context,
                              start.resolution,
                              strokePixels(context, start, end),
                              statusText);
        }
        return;
    }

    render::CanvasHit shapeStart = start;
    render::CanvasHit shapeEnd = end;
    const bool isFree = context.gradientDirection == PaletteGradientDirection::Free;
    if (isFree) {
        if (!isFreeGradientPending()) {
            beginFreeGradient(context, start, end);
            return;
        }
        const PendingFreeGradient& pending = *m_pendingFreeGradient;
        const bool sameTarget =
            onBackground ? pending.target == PaintTarget::Background &&
                               pending.backgroundIndex == context.selectedBackground
                         : pending.target == PaintTarget::Frame;
        if (!sameTarget || pending.shapeStart.region != start.region ||
            pending.shapeStart.resolution != start.resolution) {
            cancelFreeGradient();
            showStatus(QStringLiteral("Free gradient selection cancelled"), kStatusMs);
            return;
        }
        shapeStart = pending.shapeStart;
        shapeEnd = pending.shapeEnd;
    }
    if (onBackground) {
        commitBackgroundImageGradient(context,
                                      shapeStart.resolution,
                                      shapeStart,
                                      shapeEnd,
                                      start,
                                      end,
                                      statusText);
    } else {
        commitFrameGradient(context,
                            shapeStart.resolution,
                            shapeStart,
                            shapeEnd,
                            start,
                            end,
                            statusText);
    }
    if (isFree) {
        cancelFreeGradient();
    }
}

void CanvasEditController::pickDynamicSet(const CanvasEditContext& context,
                                          const render::CanvasHit& hit)
{
    if (!context.document || !context.currentFrame ||
        !isDynamicPlane(context, hit.resolution)) {
        return;
    }
    try {
        const uint8_t dynamicSet = context.document->dynamicMaskPixel(*context.currentFrame,
                                                                      hit.resolution,
                                                                      hit.x,
                                                                      hit.y);
        if (dynamicSet == 255 || dynamicSet >= document::kMaxDynamicSetsPerFrame) {
            showStatus(QStringLiteral("No dynamic set at %1,%2").arg(hit.x).arg(hit.y),
                       kStatusMs);
            return;
        }
        if (m_outputs.pickDynamicSet) {
            m_outputs.pickDynamicSet(dynamicSet);
        }
        showStatus(QStringLiteral("Picked dynamic set %1 at %2,%3")
                       .arg(static_cast<int>(dynamicSet))
                       .arg(hit.x)
                       .arg(hit.y),
                   kStatusMs);
    } catch (const std::exception& ex) {
        showError(ex);
    }
}

void CanvasEditController::pickFrameColor(const CanvasEditContext& context,
                                          const render::CanvasHit& hit,
                                          bool selectEditColorSlot)
{
    if (!context.document || !context.currentFrame) {
        return;
    }
    try {
        const uint16_t color = context.document->colorizedPixel(*context.currentFrame,
                                                                hit.resolution,
                                                                hit.x,
                                                                hit.y);
        pickedColor(color,
                    selectEditColorSlot,
                    QStringLiteral("Picked color 0x%1").arg(color, 4, 16, QLatin1Char('0')));
    } catch (const std::exception& ex) {
        showError(ex);
    }
}

void CanvasEditController::pickedColor(uint16_t color,
                                       bool selectEditColorSlot,
                                       const QString& statusText)
{
    if (selectEditColorSlot) {
        // The owner says in the status line which slot it selected, if any.
        if (m_outputs.pickEditColorSlot) {
            m_outputs.pickEditColorSlot(color);
        }
        return;
    }
    if (m_outputs.pickColor) {
        m_outputs.pickColor(color);
    }
    showStatus(statusText, kStatusMs);
}

void CanvasEditController::floodFillFrame(const CanvasEditContext& context,
                                          const render::CanvasHit& hit)
{
    if (!context.document || !context.currentFrame) {
        return;
    }
    const std::size_t frameIndex = *context.currentFrame;
    try {
        const document::Rgb565Frame& frame =
            context.document->colorizedFrame(frameIndex, hit.resolution);
        const uint16_t targetColor =
            context.document->colorizedPixel(frameIndex, hit.resolution, hit.x, hit.y);
        if (targetColor == context.drawColor &&
            context.drawColorMode == edit::DrawColorMode::SingleColor) {
            return;
        }
        // The region does not depend on the color mode: the pixels of the current frame
        // connected to the clicked one that have its color, as in the Win32 editor, whose
        // drawfill ignores the mode too. Color set replace then colors each selected
        // frame's pixels of that region by their own original shades.
        commitFramePixels(context,
                          hit.resolution,
                          edit::floodFillMatching(frame.pixels,
                                                  frame.size,
                                                  render::Point{hit.x, hit.y},
                                                  targetColor),
                          QStringLiteral("Flood filled area"));
    } catch (const std::exception& ex) {
        showError(ex);
    }
}

void CanvasEditController::pickBackgroundImageColor(const CanvasEditContext& context,
                                                    const render::CanvasHit& hit,
                                                    bool selectEditColorSlot)
{
    if (!context.document || !context.selectedBackground) {
        return;
    }
    try {
        const uint16_t color = context.document->backgroundPixel(*context.selectedBackground,
                                                                 hit.resolution,
                                                                 hit.x,
                                                                 hit.y);
        pickedColor(
            color,
            selectEditColorSlot,
            QStringLiteral("Picked background color 0x%1").arg(color, 4, 16, QLatin1Char('0')));
    } catch (const std::exception& ex) {
        showError(ex);
    }
}

void CanvasEditController::floodFillBackgroundImage(const CanvasEditContext& context,
                                                    const render::CanvasHit& hit)
{
    if (!context.document || !context.selectedBackground) {
        return;
    }
    const std::size_t backgroundIndex = *context.selectedBackground;
    try {
        const document::Rgb565Frame& frame =
            context.document->backgroundFrame(backgroundIndex, hit.resolution);
        const uint16_t targetColor =
            context.document->backgroundPixel(backgroundIndex, hit.resolution, hit.x, hit.y);
        if (targetColor == context.drawColor &&
            context.drawColorMode == edit::DrawColorMode::SingleColor) {
            return;
        }
        commitBackgroundImagePixels(context,
                                    hit.resolution,
                                    edit::floodFillMatching(frame.pixels,
                                                            frame.size,
                                                            render::Point{hit.x, hit.y},
                                                            targetColor),
                                    QStringLiteral("Flood filled background"));
    } catch (const std::exception& ex) {
        showError(ex);
    }
}

void CanvasEditController::paintComparisonMaskPixel(const CanvasEditContext& context,
                                                    const render::CanvasHit& hit,
                                                    bool erase)
{
    if (!context.document || context.layer != CanvasEditLayer::ComparisonMask) {
        return;
    }
    if (context.tool == DrawingTool::FloodFill) {
        floodFillComparisonMask(context, hit, erase);
        return;
    }
    if (context.tool != DrawingTool::Point) {
        return;
    }
    commitComparisonMaskPixels(context,
                               {render::Point{hit.x, hit.y}},
                               erase ? uint8_t{0} : uint8_t{1},
                               erase ? QStringLiteral("Erased comparison mask pixel")
                                     : QStringLiteral("Painted comparison mask pixel"));
}

void CanvasEditController::floodFillComparisonMask(const CanvasEditContext& context,
                                                   const render::CanvasHit& hit,
                                                   bool erase)
{
    const document::ProjectDocument& document = *context.document;
    std::optional<std::size_t> maskIndex;
    if (context.currentFrame) {
        const uint8_t maskId = document.comparisonMaskId(*context.currentFrame);
        if (maskId < document.comparisonMaskCount()) {
            maskIndex = static_cast<std::size_t>(maskId);
        }
    }
    if (!maskIndex) {
        showStatus(QStringLiteral("No comparison mask assigned"), kStatusMs);
        return;
    }

    try {
        const document::IndexedFrame& mask = document.comparisonMask(*maskIndex);
        const uint8_t targetValue = document.comparisonMaskPixel(*maskIndex, hit.x, hit.y);
        const uint8_t newValue = erase ? uint8_t{0} : uint8_t{1};
        if (targetValue == newValue) {
            return;
        }
        commitComparisonMaskPixels(context,
                                   edit::floodFillMatching(mask.pixels,
                                                           mask.size,
                                                           render::Point{hit.x, hit.y},
                                                           targetValue),
                                   newValue,
                                   erase ? QStringLiteral("Flood erased comparison mask")
                                         : QStringLiteral("Flood filled comparison mask"));
    } catch (const std::exception& ex) {
        showError(ex);
    }
}

void CanvasEditController::floodFillDynamicMask(const CanvasEditContext& context,
                                                const render::CanvasHit& hit,
                                                bool erase)
{
    if (!context.document || !context.currentFrame) {
        return;
    }
    const document::ProjectDocument& document = *context.document;
    const std::size_t frameIndex = *context.currentFrame;
    if (!isDynamicPlane(context, hit.resolution)) {
        return;
    }
    try {
        const uint8_t targetValue =
            document.dynamicMaskPixel(frameIndex, hit.resolution, hit.x, hit.y);
        const uint8_t newValue = erase ? uint8_t{255} : context.dynamicSet;
        if (targetValue == newValue) {
            return;
        }

        const document::IndexedFrame& mask = hit.resolution == render::Resolution::Hd
                                                 ? document.dynamicData(frameIndex)->maskHd
                                                 : document.dynamicData(frameIndex)->maskSd;
        commitFrameMaskPixels(context,
                              edit::EditKind::DynamicMask,
                              hit.resolution,
                              edit::floodFillMatching(mask.pixels,
                                                      mask.size,
                                                      render::Point{hit.x, hit.y},
                                                      targetValue),
                              newValue,
                              erase ? QStringLiteral("Flood erased dynamic mask")
                                    : QStringLiteral("Flood filled dynamic mask"));
    } catch (const std::exception& ex) {
        showError(ex);
    }
}

void CanvasEditController::floodFillBackgroundMask(const CanvasEditContext& context,
                                                   const render::CanvasHit& hit,
                                                   bool erase)
{
    if (!context.document || !context.currentFrame) {
        return;
    }
    const document::ProjectDocument& document = *context.document;
    const std::size_t frameIndex = *context.currentFrame;
    try {
        const uint8_t targetValue =
            document.backgroundMaskPixel(frameIndex, hit.resolution, hit.x, hit.y);
        const uint8_t newValue = erase ? uint8_t{0} : uint8_t{1};
        if (targetValue == newValue) {
            return;
        }

        const std::optional<document::FrameBackgroundData>& backgroundData =
            document.backgroundData(frameIndex);
        if (!backgroundData || backgroundData->backgroundId >= document.backgroundCount()) {
            showStatus(QStringLiteral("No background assigned"), kStatusMs);
            return;
        }
        const document::IndexedFrame& mask = hit.resolution == render::Resolution::Hd
                                                 ? backgroundData->maskHd
                                                 : backgroundData->maskSd;
        commitFrameMaskPixels(context,
                              edit::EditKind::BackgroundMask,
                              hit.resolution,
                              edit::floodFillMatching(mask.pixels,
                                                      mask.size,
                                                      render::Point{hit.x, hit.y},
                                                      targetValue),
                              newValue,
                              erase ? QStringLiteral("Flood erased background mask")
                                    : QStringLiteral("Flood filled background mask"));
    } catch (const std::exception& ex) {
        showError(ex);
    }
}

void CanvasEditController::paintCaptureSelection(const CanvasEditContext& context,
                                                 const std::vector<render::Point>& pixels,
                                                 bool erase)
{
    if (!context.document || !context.currentFrame || pixels.empty()) {
        return;
    }
    const render::ProjectGeometry& geometry = context.document->geometry();
    if (geometry != m_captureGeometry) {
        m_captureSelection = edit::FramePixelSelection(geometry.romFrameSize());
        m_captureGeometry = geometry;
    }
    if (!m_captureSelection.paint(*context.currentFrame, pixels, erase)) {
        return;
    }
    showCaptureSelection(context.currentFrame);
    if (m_outputs.captureSelectionChanged) {
        m_outputs.captureSelectionChanged();
    }
}

bool CanvasEditController::isFreeGradientPending() const
{
    return m_pendingFreeGradient.has_value();
}

void CanvasEditController::cancelFreeGradient()
{
    m_pendingFreeGradient.reset();
    clearPreview();
}

void CanvasEditController::beginFreeGradient(const CanvasEditContext& context,
                                             const render::CanvasHit& start,
                                             const render::CanvasHit& end)
{
    m_pendingFreeGradient =
        PendingFreeGradient{start, end, context.paintTarget, context.selectedBackground};
    showPendingFreeGradient(context);
    showStatus(QStringLiteral("Draw gradient direction line"), kStatusMs);
}

void CanvasEditController::previewFreeGradientDirection(const CanvasEditContext& context,
                                                        const render::CanvasHit& start,
                                                        const render::CanvasHit& end)
{
    if (!m_pendingFreeGradient) {
        return;
    }
    const PendingFreeGradient& pending = *m_pendingFreeGradient;
    showPreview(pending.shapeStart.region,
                pending.shapeStart.resolution,
                withLine(strokePixels(context, pending.shapeStart, pending.shapeEnd), start, end),
                translucentDrawColor(context, 145));
}

void CanvasEditController::showPendingFreeGradient(const CanvasEditContext& context)
{
    if (!m_pendingFreeGradient) {
        return;
    }
    const PendingFreeGradient& pending = *m_pendingFreeGradient;
    showPreview(pending.shapeStart.region,
                pending.shapeStart.resolution,
                strokePixels(context, pending.shapeStart, pending.shapeEnd),
                translucentDrawColor(context, 120));
}

bool CanvasEditController::hasSelection() const
{
    return m_selection.has_value();
}

bool CanvasEditController::isSelectionDirectionMode() const
{
    return m_selectionDirectionMode;
}

bool CanvasEditController::isSelectionDirectionModeActive() const
{
    return m_selectionDirectionMode && m_selection.has_value();
}

void CanvasEditController::setSelectionFromStroke(const CanvasEditContext& context,
                                                  const render::CanvasHit& start,
                                                  const render::CanvasHit& end)
{
    m_selectionDirectionMode = false;
    selectionDirectionModeChanged();
    if (start.region != render::CanvasRegion::ColorizedFrame ||
        start.resolution != end.resolution) {
        return;
    }
    edit::PlaneSelection pixels(start.resolution,
                                projectGeometry(context).planeSize(start.resolution));
    pixels.add(strokePixels(context, start, end));
    if (pixels.isEmpty()) {
        clearSelection(context);
        return;
    }
    m_selection = Selection{
        start,
        end,
        context.paintTarget,
        context.selectedBackground,
        std::move(pixels),
        std::nullopt,
        std::nullopt,
    };
    showSelection();
    selectionChanged();
    showStatus(QStringLiteral("Selected canvas area"), kShortStatusMs);
}

void CanvasEditController::toggleSelectionDirectionMode()
{
    if (!m_selection) {
        m_selectionDirectionMode = false;
        selectionDirectionModeChanged();
        selectionChanged();
        return;
    }
    m_selectionDirectionMode = !m_selectionDirectionMode;
    selectionDirectionModeChanged();
    if (m_selectionDirectionMode) {
        cancelFreeGradient();
        showStatus(QStringLiteral("Draw selection gradient direction line"), kStatusMs);
    } else {
        showSelection();
    }
}

void CanvasEditController::leaveSelectionDirectionMode()
{
    m_selectionDirectionMode = false;
}

void CanvasEditController::clearSelection(const CanvasEditContext& context)
{
    m_selection.reset();
    m_selectionDirectionMode = false;
    selectionDirectionModeChanged();
    // The overlay shows the capture selection while that is being painted.
    if (!context.spriteCaptureSelection) {
        clearSelectionOverlay();
    }
    selectionChanged();
}

void CanvasEditController::showSelection()
{
    if (!m_selection) {
        return;
    }
    std::vector<render::Point> pixels = m_selection->pixels.pixels();
    if (m_selection->gradientStart && m_selection->gradientEnd) {
        pixels =
            withLine(std::move(pixels), *m_selection->gradientStart, *m_selection->gradientEnd);
    }
    showSelectionOverlay(m_selection->shapeStart.region,
                         m_selection->shapeStart.resolution,
                         std::move(pixels),
                         QColor(0, 220, 180, 95));
}

void CanvasEditController::setSelectionDirection(const render::CanvasHit& start,
                                                 const render::CanvasHit& end)
{
    if (!m_selection || start.region != m_selection->shapeStart.region ||
        end.region != m_selection->shapeStart.region ||
        start.resolution != m_selection->shapeStart.resolution ||
        end.resolution != m_selection->shapeStart.resolution) {
        return;
    }
    m_selection->gradientStart = start;
    m_selection->gradientEnd = end;
    m_selectionDirectionMode = false;
    selectionDirectionModeChanged();
    showSelection();
    showStatus(QStringLiteral("Set selection gradient direction"), kShortStatusMs);
}

void CanvasEditController::previewSelectionDirection(const render::CanvasHit& start,
                                                     const render::CanvasHit& end)
{
    if (!m_selection || start.region != m_selection->shapeStart.region ||
        start.resolution != m_selection->shapeStart.resolution ||
        end.region != m_selection->shapeStart.region ||
        end.resolution != m_selection->shapeStart.resolution) {
        return;
    }
    showSelectionOverlay(m_selection->shapeStart.region,
                         m_selection->shapeStart.resolution,
                         withLine(m_selection->pixels.pixels(), start, end),
                         QColor(0, 220, 180, 120));
}

void CanvasEditController::fillSelectionWithPaletteGradient(const CanvasEditContext& context)
{
    if (!context.document || !m_selection || m_selection->pixels.isEmpty()) {
        showStatus(QStringLiteral("No canvas selection"), kStatusMs);
        return;
    }
    if (context.gradientDirection == PaletteGradientDirection::Free &&
        (!m_selection->gradientStart || !m_selection->gradientEnd)) {
        showStatus(QStringLiteral("Set a selection direction line first"), kLongStatusMs);
        return;
    }
    if (!context.selectedPalette) {
        showStatus(QStringLiteral("No palette selected"), kStatusMs);
        return;
    }
    if (m_selection->target == PaintTarget::Background &&
        m_selection->backgroundIndex != context.selectedBackground) {
        clearSelection(context);
        showStatus(QStringLiteral("Canvas selection cleared"), kStatusMs);
        return;
    }

    const document::ProjectDocument& document = *context.document;
    const render::CanvasHit shapeStart = m_selection->shapeStart;
    const render::CanvasHit shapeEnd = m_selection->shapeEnd;
    const edit::PixelColorSource colorForPoint =
        gradientColorSource(context,
                            *context.selectedPalette,
                            shapeStart,
                            shapeEnd,
                            m_selection->gradientStart.value_or(shapeStart),
                            m_selection->gradientEnd.value_or(shapeEnd));

    try {
        if (m_selection->target == PaintTarget::Background) {
            if (!context.selectedBackground) {
                return;
            }
            const edit::EditLayer layer{edit::EditKind::BackgroundImage,
                                        *context.selectedBackground,
                                        shapeStart.resolution};
            if (!commitPixelChanges(layer,
                                    edit::makePixelChanges(document,
                                                           layer,
                                                           m_selection->pixels.pixels(),
                                                           colorForPoint))) {
                return;
            }
        } else {
            const std::vector<render::Point> pixels = m_selection->pixels.pixels();
            bool changed = false;
            for (std::size_t frameIndex : context.selectedFrames) {
                if (shapeStart.resolution == render::Resolution::Hd &&
                    !document.hasHdFrame(frameIndex)) {
                    continue;
                }
                const edit::EditLayer layer{edit::EditKind::FramePixels,
                                            frameIndex,
                                            shapeStart.resolution};
                if (commitPixelChanges(layer,
                                       edit::makePixelChanges(document,
                                                              layer,
                                                              pixels,
                                                              colorForPoint))) {
                    changed = true;
                }
            }
            if (!changed) {
                return;
            }
        }
        commitsFinished();
        showStatus(QStringLiteral("Filled selection gradient"), kStatusMs);
    } catch (const std::exception& ex) {
        showError(ex);
    }
}

const edit::PlaneSelection* CanvasEditController::selection() const
{
    return m_selection && !m_selection->pixels.isEmpty() ? &m_selection->pixels : nullptr;
}

bool CanvasEditController::canEditSelection(const CanvasEditContext& context)
{
    return context.document && !context.spriteCaptureSelection &&
           (context.layer == CanvasEditLayer::FramePixels ||
            context.layer == CanvasEditLayer::BackgroundImage);
}

CanvasEditController::Selection CanvasEditController::selectionFor(
    const CanvasEditContext& context,
    render::Resolution plane) const
{
    const bool onBackground = context.layer == CanvasEditLayer::BackgroundImage;
    const PaintTarget target = onBackground ? PaintTarget::Background : PaintTarget::Frame;
    const std::optional<std::size_t> backgroundIndex =
        onBackground ? context.selectedBackground : std::nullopt;
    if (m_selection && m_selection->pixels.plane() == plane && m_selection->target == target &&
        (!onBackground || m_selection->backgroundIndex == backgroundIndex)) {
        return *m_selection;
    }
    const render::CanvasHit corner{render::CanvasRegion::ColorizedFrame, plane, 0, 0};
    return Selection{corner,
                     corner,
                     target,
                     backgroundIndex,
                     edit::PlaneSelection(plane, projectGeometry(context).planeSize(plane)),
                     std::nullopt,
                     std::nullopt};
}

void CanvasEditController::setSelection(const CanvasEditContext& context,
                                        Selection selection,
                                        const QString& status)
{
    m_selectionDirectionMode = false;
    selectionDirectionModeChanged();
    const std::optional<render::Rect> bounds = selection.pixels.bounds();
    if (!bounds) {
        clearSelection(context);
        if (!status.isEmpty()) {
            showStatus(status, kShortStatusMs);
        }
        return;
    }
    const render::Resolution plane = selection.pixels.plane();
    selection.shapeStart = render::CanvasHit{render::CanvasRegion::ColorizedFrame,
                                             plane,
                                             bounds->x,
                                             bounds->y};
    selection.shapeEnd = render::CanvasHit{render::CanvasRegion::ColorizedFrame,
                                           plane,
                                           bounds->x + bounds->width - 1,
                                           bounds->y + bounds->height - 1};
    selection.gradientStart.reset();
    selection.gradientEnd.reset();
    m_selection = std::move(selection);
    showSelection();
    selectionChanged();
    if (!status.isEmpty()) {
        showStatus(QStringLiteral("%1 (%2 pixels selected)")
                       .arg(status)
                       .arg(static_cast<qulonglong>(m_selection->pixels.count())),
                   kShortStatusMs);
    }
}

const document::Rgb565Frame* CanvasEditController::selectionPicture(
    const CanvasEditContext& context,
    render::Resolution plane) const
{
    if (!context.document) {
        return nullptr;
    }
    const document::ProjectDocument& document = *context.document;
    if (!document.geometry().isEditable(plane)) {
        return nullptr;
    }
    if (context.layer == CanvasEditLayer::BackgroundImage) {
        if (!context.selectedBackground ||
            *context.selectedBackground >= document.backgroundCount()) {
            return nullptr;
        }
        const document::BackgroundRecord& background =
            document.background(*context.selectedBackground);
        if (plane == render::Resolution::Hd && !background.colorizedHd) {
            return nullptr;
        }
        return &document.backgroundFrame(*context.selectedBackground, plane);
    }
    if (!context.currentFrame ||
        (plane == render::Resolution::Hd && !document.hasHdFrame(*context.currentFrame))) {
        return nullptr;
    }
    return &document.colorizedFrame(*context.currentFrame, plane);
}

void CanvasEditController::editSelectionAtPoint(const CanvasEditContext& context,
                                                const render::CanvasHit& hit,
                                                bool remove,
                                                bool nonBlackOnly)
{
    if (!canEditSelection(context) || hit.region != render::CanvasRegion::ColorizedFrame) {
        return;
    }
    const document::Rgb565Frame* picture = selectionPicture(context, hit.resolution);
    if (!picture) {
        showStatus(QStringLiteral("Nothing to select on this plane"), kStatusMs);
        return;
    }
    const render::Point point{hit.x, hit.y};
    std::vector<render::Point> pixels;
    if (context.tool == DrawingTool::FloodFill) {
        const uint16_t color = picture->pixels[static_cast<std::size_t>(hit.y) *
                                                   static_cast<std::size_t>(picture->size.width) +
                                               static_cast<std::size_t>(hit.x)];
        pixels = edit::floodFillMatching(picture->pixels, picture->size, point, color);
    } else if (context.tool == DrawingTool::Point) {
        pixels = {point};
    } else {
        // The other tools select by dragging; their click is the first point of the drag.
        return;
    }
    if (nonBlackOnly && !remove) {
        pixels = edit::nonBlackPixels(*picture, pixels);
    }
    Selection selection = selectionFor(context, hit.resolution);
    if (!selection.pixels.paint(pixels, remove)) {
        return;
    }
    setSelection(context,
                 std::move(selection),
                 remove ? QStringLiteral("Removed from the selection")
                        : QStringLiteral("Added to the selection"));
}

void CanvasEditController::previewSelectionStroke(const CanvasEditContext& context,
                                                  const render::CanvasHit& start,
                                                  const render::CanvasHit& end,
                                                  bool remove)
{
    if (!canEditSelection(context) || start.region != render::CanvasRegion::ColorizedFrame ||
        start.region != end.region || start.resolution != end.resolution) {
        return;
    }
    showPreview(render::CanvasRegion::ColorizedFrame,
                start.resolution,
                strokePixels(context, start, end),
                remove ? QColor(0, 0, 0, 150) : QColor(0, 220, 180, 130));
}

void CanvasEditController::editSelectionWithStroke(const CanvasEditContext& context,
                                                   const render::CanvasHit& start,
                                                   const render::CanvasHit& end,
                                                   bool remove,
                                                   bool nonBlackOnly)
{
    clearPreview();
    if (!canEditSelection(context) || start.region != render::CanvasRegion::ColorizedFrame ||
        start.region != end.region || start.resolution != end.resolution) {
        return;
    }
    const DrawingTool tool = context.tool;
    if (tool == DrawingTool::Point || tool == DrawingTool::FloodFill ||
        tool == DrawingTool::ColorPicker) {
        return;
    }
    const document::Rgb565Frame* picture = selectionPicture(context, start.resolution);
    if (!picture) {
        showStatus(QStringLiteral("Nothing to select on this plane"), kStatusMs);
        return;
    }
    std::vector<render::Point> pixels = strokePixels(context, start, end);
    if (nonBlackOnly && !remove) {
        pixels = edit::nonBlackPixels(*picture, pixels);
    }
    Selection selection = selectionFor(context, start.resolution);
    if (!selection.pixels.paint(pixels, remove)) {
        return;
    }
    setSelection(context,
                 std::move(selection),
                 remove ? QStringLiteral("Removed from the selection")
                        : QStringLiteral("Added to the selection"));
}

void CanvasEditController::selectAll(const CanvasEditContext& context)
{
    if (!canEditSelection(context) || !selectionPicture(context, context.activePlane)) {
        showStatus(QStringLiteral("Nothing to select"), kStatusMs);
        return;
    }
    Selection selection = selectionFor(context, context.activePlane);
    selection.pixels.selectAll();
    setSelection(context, std::move(selection), QStringLiteral("Selected all"));
}

void CanvasEditController::invertSelection(const CanvasEditContext& context)
{
    if (!canEditSelection(context) || !selectionPicture(context, context.activePlane)) {
        showStatus(QStringLiteral("Nothing to select"), kStatusMs);
        return;
    }
    Selection selection = selectionFor(context, context.activePlane);
    selection.pixels.invert();
    setSelection(context, std::move(selection), QStringLiteral("Inverted the selection"));
}

void CanvasEditController::growSelection(const CanvasEditContext& context,
                                         edit::SelectionDirection direction)
{
    if (!m_selection || m_selection->pixels.isEmpty()) {
        showStatus(QStringLiteral("No canvas selection"), kStatusMs);
        return;
    }
    Selection selection = *m_selection;
    selection.pixels = edit::grownSelection(selection.pixels, direction);
    setSelection(context, std::move(selection), QStringLiteral("Grew the selection"));
}

void CanvasEditController::mirrorSelection(const CanvasEditContext& context,
                                           bool horizontally,
                                           bool vertically)
{
    if (!m_selection || m_selection->pixels.isEmpty()) {
        showStatus(QStringLiteral("No canvas selection"), kStatusMs);
        return;
    }
    Selection selection = *m_selection;
    selection.pixels.mirror(horizontally, vertically);
    setSelection(context,
                 std::move(selection),
                 horizontally ? QStringLiteral("Mirrored the selection horizontally")
                              : QStringLiteral("Mirrored the selection vertically"));
}

void CanvasEditController::selectByColor(const CanvasEditContext& context,
                                         const render::CanvasHit& hit,
                                         bool remove)
{
    if (!canEditSelection(context) || hit.region != render::CanvasRegion::ColorizedFrame) {
        return;
    }
    const document::Rgb565Frame* stored = selectionPicture(context, hit.resolution);
    if (!stored) {
        showStatus(QStringLiteral("Nothing to select on this plane"), kStatusMs);
        return;
    }
    try {
        document::Rgb565Frame shown = *stored;
        if (context.layer == CanvasEditLayer::FramePixels) {
            shown = composeResolvedFrame(*context.document,
                                         *context.currentFrame,
                                         hit.resolution,
                                         context.includeBackgroundLayer);
        }
        const uint16_t color = shown.pixels[static_cast<std::size_t>(hit.y) *
                                                static_cast<std::size_t>(shown.size.width) +
                                            static_cast<std::size_t>(hit.x)];
        Selection selection = selectionFor(context, hit.resolution);
        selection.pixels.paint(edit::pixelsOfColor(shown, color), remove);
        setSelection(context,
                     std::move(selection),
                     QStringLiteral("%1 the pixels of color 0x%2")
                         .arg(remove ? QStringLiteral("Unselected") : QStringLiteral("Selected"))
                         .arg(color, 4, 16, QLatin1Char('0')));
    } catch (const std::exception& ex) {
        showError(ex);
    }
}

void CanvasEditController::selectBackgroundMask(const CanvasEditContext& context)
{
    if (!canEditSelection(context) || context.layer != CanvasEditLayer::FramePixels ||
        !context.currentFrame || !selectionPicture(context, context.activePlane)) {
        showStatus(QStringLiteral("The selection from the background mask needs a frame"),
                   kStatusMs);
        return;
    }
    const document::ProjectDocument& document = *context.document;
    const std::optional<document::FrameBackgroundData>& backgroundData =
        document.backgroundData(*context.currentFrame);
    if (!backgroundData || backgroundData->backgroundId >= document.backgroundCount()) {
        showStatus(QStringLiteral("The frame has no background"), kStatusMs);
        return;
    }
    const document::IndexedFrame& mask = context.activePlane == render::Resolution::Hd
                                             ? backgroundData->maskHd
                                             : backgroundData->maskSd;
    Selection selection = selectionFor(context, context.activePlane);
    try {
        selection.pixels.assignMask(mask.pixels);
    } catch (const std::exception& ex) {
        showError(ex);
        return;
    }
    setSelection(context, std::move(selection), QStringLiteral("Selected the background mask"));
}

void CanvasEditController::selectStripes(const CanvasEditContext& context,
                                         const edit::StripePattern& pattern)
{
    if (!canEditSelection(context) || !selectionPicture(context, context.activePlane)) {
        showStatus(QStringLiteral("Nothing to select"), kStatusMs);
        return;
    }
    Selection selection = selectionFor(context, context.activePlane);
    try {
        selection.pixels.assignMask(edit::stripeMask(selection.pixels.size(), pattern));
    } catch (const std::exception& ex) {
        showError(ex);
        return;
    }
    setSelection(context,
                 std::move(selection),
                 pattern.columns ? QStringLiteral("Selected columns")
                                 : QStringLiteral("Selected rows"));
}

std::optional<std::size_t> CanvasEditController::captureSelectionFrame() const
{
    return m_captureSelection.frameIndex();
}

std::vector<render::Point> CanvasEditController::captureSelectionPixels(
    std::optional<std::size_t> frameIndex) const
{
    return m_captureSelection.pixels(frameIndex);
}

void CanvasEditController::showCaptureSelection(std::optional<std::size_t> frameIndex)
{
    std::vector<render::Point> pixels = m_captureSelection.pixels(frameIndex);
    if (pixels.empty()) {
        clearSelectionOverlay();
        return;
    }
    showSelectionOverlay(render::CanvasRegion::OriginalFrame,
                         m_captureGeometry.nativePlane(),
                         std::move(pixels),
                         QColor(255, 0, 255, 110));
}

void CanvasEditController::clearCaptureSelection()
{
    m_captureSelection.clear();
    clearSelectionOverlay();
    if (m_outputs.captureSelectionChanged) {
        m_outputs.captureSelectionChanged();
    }
}

void CanvasEditController::resetCaptureSelection()
{
    m_captureSelection.clear();
}

std::vector<render::Point> CanvasEditController::strokePixels(const CanvasEditContext& context,
                                                              const render::CanvasHit& start,
                                                              const render::CanvasHit& end)
{
    return edit::makeStrokePixels(context.tool,
                                  context.gradientShape,
                                  render::Point{start.x, start.y},
                                  render::Point{end.x, end.y});
}

edit::PixelColorSource CanvasEditController::gradientColorSource(
    const CanvasEditContext& context,
    std::size_t paletteIndex,
    const render::CanvasHit& shapeStart,
    const render::CanvasHit& shapeEnd,
    const render::CanvasHit& gradientStart,
    const render::CanvasHit& gradientEnd)
{
    const std::pair<int, int> endpoints =
        context.gradientEndpoints ? context.gradientEndpoints() : std::pair<int, int>{0, 0};
    return edit::paletteGradientColorSource(*context.document,
                                            paletteIndex,
                                            edit::PaletteGradient{
                                                render::Point{shapeStart.x, shapeStart.y},
                                                render::Point{shapeEnd.x, shapeEnd.y},
                                                render::Point{gradientStart.x, gradientStart.y},
                                                render::Point{gradientEnd.x, gradientEnd.y},
                                                context.gradientDirection,
                                                context.gradientPattern,
                                                endpoints.first,
                                                endpoints.second,
                                            });
}

void CanvasEditController::commitFramePixels(const CanvasEditContext& context,
                                             render::Resolution resolution,
                                             const std::vector<render::Point>& pixels,
                                             const QString& statusText)
{
    if (context.drawColorMode == edit::DrawColorMode::ColorSetReplace && context.document) {
        const document::ProjectDocument& document = *context.document;
        commitFramePixelColors(
            context,
            resolution,
            pixels,
            [&document, resolution](std::size_t frameIndex) {
                return edit::colorSetReplaceColorSource(document, frameIndex, resolution);
            },
            statusText);
        return;
    }
    const uint16_t drawColor = context.drawColor;
    commitFramePixelColors(context,
                           resolution,
                           pixels,
                           [drawColor](render::Point) { return drawColor; },
                           statusText);
}

void CanvasEditController::commitFrameGradient(const CanvasEditContext& context,
                                               render::Resolution resolution,
                                               const render::CanvasHit& start,
                                               const render::CanvasHit& end,
                                               const render::CanvasHit& gradientStart,
                                               const render::CanvasHit& gradientEnd,
                                               const QString& statusText)
{
    const std::vector<render::Point> pixels = strokePixels(context, start, end);
    if (!context.document || context.selectedFrames.empty() || pixels.empty() ||
        !context.selectedPalette) {
        showStatus(QStringLiteral("No palette gradient source selected"), kStatusMs);
        return;
    }
    commitFramePixelColors(context,
                           resolution,
                           pixels,
                           gradientColorSource(context,
                                               *context.selectedPalette,
                                               start,
                                               end,
                                               gradientStart,
                                               gradientEnd),
                           statusText);
}

void CanvasEditController::commitFramePixelColors(const CanvasEditContext& context,
                                                  render::Resolution resolution,
                                                  const std::vector<render::Point>& pixels,
                                                  const edit::PixelColorSource& colorForPoint,
                                                  const QString& statusText)
{
    commitFramePixelColors(
        context,
        resolution,
        pixels,
        [&colorForPoint](std::size_t) { return colorForPoint; },
        statusText);
}

void CanvasEditController::commitFramePixelColors(
    const CanvasEditContext& context,
    render::Resolution resolution,
    const std::vector<render::Point>& pixels,
    const std::function<edit::PixelColorSource(std::size_t frameIndex)>& sourceForFrame,
    const QString& statusText)
{
    const std::vector<std::size_t>& frameIndices = context.selectedFrames;
    if (!context.document || frameIndices.empty() || pixels.empty()) {
        return;
    }
    const document::ProjectDocument& document = *context.document;

    try {
        bool changed = false;
        for (std::size_t frameIndex : frameIndices) {
            if (resolution == render::Resolution::Hd && !document.hasHdFrame(frameIndex)) {
                continue;
            }
            const edit::EditLayer layer{edit::EditKind::FramePixels, frameIndex, resolution};
            if (commitPixelChanges(layer,
                                   edit::makePixelChanges(document,
                                                          layer,
                                                          pixels,
                                                          sourceForFrame(frameIndex)))) {
                changed = true;
            }
        }
        if (!changed) {
            return;
        }
        commitsFinished();
    } catch (const std::exception& ex) {
        showError(ex);
        return;
    }

    showStatus(onFramesText(statusText, frameIndices.size()), kStatusMs);
}

void CanvasEditController::commitComparisonMaskPixels(const CanvasEditContext& context,
                                                      const std::vector<render::Point>& pixels,
                                                      uint8_t value,
                                                      const QString& statusText)
{
    if (!context.document || pixels.empty()) {
        return;
    }
    const document::ProjectDocument& document = *context.document;
    std::set<std::size_t> maskIndices;
    for (std::size_t frameIndex : context.selectedFrames) {
        const uint8_t maskId = document.comparisonMaskId(frameIndex);
        if (maskId < document.comparisonMaskCount()) {
            maskIndices.insert(static_cast<std::size_t>(maskId));
        }
    }
    if (maskIndices.empty()) {
        showStatus(QStringLiteral("No comparison mask assigned"), kStatusMs);
        return;
    }

    try {
        bool changed = false;
        for (std::size_t maskIndex : maskIndices) {
            const edit::EditLayer layer{edit::EditKind::ComparisonMask,
                                        maskIndex,
                                        render::Resolution::Sd};
            if (commitMaskChanges(layer, edit::makeMaskChanges(document, layer, pixels, value))) {
                changed = true;
            }
        }
        if (!changed) {
            return;
        }
        commitsFinished();
    } catch (const std::exception& ex) {
        showError(ex);
        return;
    }

    showStatus(maskIndices.size() > 1 ? QStringLiteral("%1 on %2 mask(s)")
                                            .arg(statusText)
                                            .arg(static_cast<int>(maskIndices.size()))
                                      : statusText,
               kStatusMs);
}

bool CanvasEditController::isDynamicPlane(const CanvasEditContext& context,
                                          render::Resolution resolution)
{
    if (!context.document || resolution == context.document->dynamicPlane()) {
        return true;
    }
    const render::Size size =
        context.document->geometry().planeSize(context.document->dynamicPlane());
    showStatus(QStringLiteral("Dynamic zones are defined on the %1x%2 plane and cannot be "
                              "picked or painted on this one")
                   .arg(size.width)
                   .arg(size.height),
               kStatusMs);
    return false;
}

void CanvasEditController::commitFrameMaskPixels(const CanvasEditContext& context,
                                                 edit::EditKind kind,
                                                 render::Resolution resolution,
                                                 const std::vector<render::Point>& pixels,
                                                 uint8_t value,
                                                 const QString& statusText)
{
    const std::vector<std::size_t>& frameIndices = context.selectedFrames;
    if (!context.document || frameIndices.empty() || pixels.empty()) {
        return;
    }
    const document::ProjectDocument& document = *context.document;
    const bool isBackgroundMask = kind == edit::EditKind::BackgroundMask;
    if (kind == edit::EditKind::DynamicMask && !isDynamicPlane(context, resolution)) {
        return;
    }

    try {
        bool changed = false;
        bool hasAssignedBackground = false;
        for (std::size_t frameIndex : frameIndices) {
            if (isBackgroundMask) {
                const std::optional<document::FrameBackgroundData>& backgroundData =
                    document.backgroundData(frameIndex);
                if (!backgroundData ||
                    backgroundData->backgroundId >= document.backgroundCount()) {
                    continue;
                }
                hasAssignedBackground = true;
            } else if (!document.dynamicData(frameIndex)) {
                continue;
            }
            const edit::EditLayer layer{kind, frameIndex, resolution};
            if (commitMaskChanges(layer, edit::makeMaskChanges(document, layer, pixels, value))) {
                changed = true;
            }
        }
        if (!changed) {
            if (isBackgroundMask && !hasAssignedBackground) {
                showStatus(QStringLiteral("No selected frame has an assigned background"),
                           kStatusMs);
            }
            return;
        }
        commitsFinished();
    } catch (const std::exception& ex) {
        showError(ex);
        return;
    }

    showStatus(onFramesText(statusText, frameIndices.size()), kStatusMs);
}

void CanvasEditController::commitBackgroundImagePixels(const CanvasEditContext& context,
                                                       render::Resolution resolution,
                                                       const std::vector<render::Point>& pixels,
                                                       const QString& statusText)
{
    if (!context.document || pixels.empty()) {
        return;
    }
    const uint16_t drawColor = context.drawColor;
    if (!context.selectedBackground) {
        showStatus(QStringLiteral("No background selected"), kStatusMs);
        return;
    }
    // A background has no original frame; color set replace takes the shades of the frame it
    // is painted over.
    if (context.drawColorMode == edit::DrawColorMode::ColorSetReplace && context.currentFrame) {
        try {
            commitBackgroundImageColors(context,
                                        *context.selectedBackground,
                                        resolution,
                                        pixels,
                                        edit::colorSetReplaceColorSource(*context.document,
                                                                         *context.currentFrame,
                                                                         resolution),
                                        statusText);
        } catch (const std::exception& ex) {
            showError(ex);
        }
        return;
    }
    commitBackgroundImageColors(context,
                                *context.selectedBackground,
                                resolution,
                                pixels,
                                [drawColor](render::Point) { return drawColor; },
                                statusText);
}

void CanvasEditController::commitBackgroundImageGradient(const CanvasEditContext& context,
                                                         render::Resolution resolution,
                                                         const render::CanvasHit& start,
                                                         const render::CanvasHit& end,
                                                         const render::CanvasHit& gradientStart,
                                                         const render::CanvasHit& gradientEnd,
                                                         const QString& statusText)
{
    if (!context.document) {
        return;
    }
    const std::vector<render::Point> pixels = strokePixels(context, start, end);
    if (!context.selectedBackground || !context.selectedPalette || pixels.empty()) {
        showStatus(QStringLiteral("No background or palette selected"), kStatusMs);
        return;
    }
    commitBackgroundImageColors(context,
                                *context.selectedBackground,
                                resolution,
                                pixels,
                                gradientColorSource(context,
                                                    *context.selectedPalette,
                                                    start,
                                                    end,
                                                    gradientStart,
                                                    gradientEnd),
                                statusText);
}

void CanvasEditController::commitBackgroundImageColors(const CanvasEditContext& context,
                                                       std::size_t backgroundIndex,
                                                       render::Resolution resolution,
                                                       const std::vector<render::Point>& pixels,
                                                       const edit::PixelColorSource& colorForPoint,
                                                       const QString& statusText)
{
    try {
        const edit::EditLayer layer{edit::EditKind::BackgroundImage, backgroundIndex, resolution};
        if (!commitPixelChanges(layer,
                                edit::makePixelChanges(*context.document,
                                                       layer,
                                                       pixels,
                                                       colorForPoint))) {
            return;
        }
        commitsFinished();
    } catch (const std::exception& ex) {
        showError(ex);
        return;
    }

    showStatus(statusText, kStatusMs);
}

bool CanvasEditController::commitPixelChanges(const edit::EditLayer& layer,
                                              std::vector<edit::PixelChange> changes)
{
    if (changes.empty()) {
        return false;
    }
    if (m_outputs.commit) {
        m_outputs.commit(std::make_unique<edit::PixelEditCommand>(layer.kind,
                                                                  layer.index,
                                                                  layer.resolution,
                                                                  std::move(changes)));
    }
    return true;
}

bool CanvasEditController::commitMaskChanges(const edit::EditLayer& layer,
                                             std::vector<edit::MaskPixelChange> changes)
{
    if (changes.empty()) {
        return false;
    }
    if (m_outputs.commit) {
        m_outputs.commit(std::make_unique<edit::MaskEditCommand>(layer.kind,
                                                                 layer.index,
                                                                 layer.resolution,
                                                                 std::move(changes)));
    }
    return true;
}

void CanvasEditController::showStatus(const QString& text, int timeoutMs) const
{
    if (m_outputs.showStatus) {
        m_outputs.showStatus(text, timeoutMs);
    }
}

void CanvasEditController::showError(const std::exception& error) const
{
    showStatus(QString::fromUtf8(error.what()), kErrorStatusMs);
}

void CanvasEditController::showPreview(render::CanvasRegion region,
                                       render::Resolution resolution,
                                       std::vector<render::Point> pixels,
                                       QColor color) const
{
    if (m_outputs.showPreview) {
        m_outputs.showPreview(CanvasPixelOverlay{region, resolution, std::move(pixels), color});
    }
}

void CanvasEditController::clearPreview() const
{
    if (m_outputs.clearPreview) {
        m_outputs.clearPreview();
    }
}

void CanvasEditController::showSelectionOverlay(render::CanvasRegion region,
                                                render::Resolution resolution,
                                                std::vector<render::Point> pixels,
                                                QColor color) const
{
    if (m_outputs.showSelectionOverlay) {
        m_outputs.showSelectionOverlay(
            CanvasPixelOverlay{region, resolution, std::move(pixels), color});
    }
}

void CanvasEditController::clearSelectionOverlay() const
{
    if (m_outputs.clearSelectionOverlay) {
        m_outputs.clearSelectionOverlay();
    }
}

void CanvasEditController::commitsFinished() const
{
    if (m_outputs.commitsFinished) {
        m_outputs.commitsFinished();
    }
}

void CanvasEditController::selectionChanged() const
{
    if (m_outputs.selectionChanged) {
        m_outputs.selectionChanged();
    }
}

void CanvasEditController::selectionDirectionModeChanged() const
{
    if (m_outputs.selectionDirectionModeChanged) {
        m_outputs.selectionDirectionModeChanged(m_selectionDirectionMode);
    }
}

}  // namespace colorizing::app

#include "EditHelpersController.h"

#include <exception>
#include <utility>

#include "edit/ColorSetEdits.h"
#include "edit/DrawingTools.h"
#include "edit/FrameContentCommand.h"
#include "edit/RecolorHelpers.h"

namespace colorizing::app {

namespace {

constexpr int kStatusMs = 2500;
constexpr int kLongStatusMs = 4000;

edit::EditKind historyKind(const CanvasEditContext& context)
{
    return context.layer == CanvasEditLayer::DynamicMask ? edit::EditKind::DynamicMask
                                                         : edit::EditKind::FramePixels;
}

bool editsFramePixels(const CanvasEditContext& context)
{
    return context.layer == CanvasEditLayer::FramePixels ||
           context.layer == CanvasEditLayer::DynamicMask;
}

// The selection when it lies on the plane, else nothing (the whole frame).
const edit::PlaneSelection* areaOn(const edit::PlaneSelection* selection, render::Resolution plane)
{
    return selection && selection->plane() == plane ? selection : nullptr;
}

QString framesText(std::size_t count)
{
    return QCoreApplication::translate("EditHelpersController", "%1 frame(s)")
        .arg(static_cast<qulonglong>(count));
}

}  // namespace

std::size_t ScrollFillPlan::changingCount() const
{
    std::size_t count = 0;
    for (const edit::ScrollFillFrame& frame : frames) {
        count += frame.changes.isEmpty() ? 0 : 1;
    }
    return count;
}

EditHelpersController::EditHelpersController(EditHelpersOutputs outputs)
    : m_outputs(std::move(outputs))
{
}

QString EditHelpersController::autoCopyRefusal(const CanvasEditContext& context,
                                               const edit::PlaneSelection* selection)
{
    if (!context.document || !context.currentFrame) {
        return tr("No frame");
    }
    if (!editsFramePixels(context)) {
        return tr("Auto copy works on the frame's pixels");
    }
    if (!selection) {
        return tr("Select the part of the frame to look for first");
    }
    if (selection->plane() != context.activePlane) {
        return tr("The selection is on the other plane");
    }
    if (selection->plane() == render::Resolution::Hd &&
        !context.document->hasHdFrame(*context.currentFrame)) {
        return tr("The frame has no picture on this plane");
    }
    return QString();
}

AutoCopyPlan EditHelpersController::planAutoCopy(const CanvasEditContext& context,
                                                 const edit::PlaneSelection& selection,
                                                 const std::vector<std::size_t>& candidates) const
{
    AutoCopyPlan plan;
    const document::ProjectDocument& document = *context.document;
    plan.scan = edit::scanAutoCopy(document, *context.currentFrame, selection, candidates);
    for (std::size_t frameIndex : plan.scan.matches) {
        if (!edit::makeAutoCopyChanges(document, *context.currentFrame, frameIndex, selection)
                 .isEmpty()) {
            plan.changingFrames.push_back(frameIndex);
        }
    }
    return plan;
}

void EditHelpersController::applyAutoCopy(const CanvasEditContext& context,
                                          const edit::PlaneSelection& selection,
                                          const AutoCopyPlan& plan)
{
    std::vector<std::pair<std::size_t, edit::FrameContentChanges>> changes;
    try {
        for (std::size_t frameIndex : plan.changingFrames) {
            changes.emplace_back(frameIndex,
                                 edit::makeAutoCopyChanges(*context.document,
                                                           *context.currentFrame,
                                                           frameIndex,
                                                           selection));
        }
    } catch (const std::exception& ex) {
        showStatus(QString::fromUtf8(ex.what()), kLongStatusMs);
        return;
    }
    const std::size_t changed = commitAll(context, std::move(changes));
    showStatus(tr("Auto copy: %1 updated").arg(framesText(changed)), kStatusMs);
}

QString EditHelpersController::scrollFillRefusal(const CanvasEditContext& context)
{
    if (!context.document || !context.currentFrame) {
        return tr("No frame");
    }
    if (!editsFramePixels(context)) {
        return tr("The scrolling fill works on the frame's pixels");
    }
    return QString();
}

ScrollFillPlan EditHelpersController::planScrollFill(const CanvasEditContext& context,
                                                    const edit::PlaneSelection* selection) const
{
    ScrollFillPlan plan;
    const auto [first, last] =
        edit::selectedRunAround(context.selectedFrames, *context.currentFrame);
    plan.firstFrame = first;
    plan.lastFrame = last;
    const bool useSelection =
        selection && !selection->isEmpty() && selection->plane() == context.activePlane;
    plan.selectionOnly = useSelection;
    plan.frames = edit::makeScrollFill(*context.document, context.activePlane, first, last,
                                       useSelection ? selection : nullptr);
    return plan;
}

void EditHelpersController::applyScrollFill(const CanvasEditContext& context, ScrollFillPlan plan)
{
    std::vector<std::pair<std::size_t, edit::FrameContentChanges>> changes;
    for (edit::ScrollFillFrame& frame : plan.frames) {
        changes.emplace_back(frame.frameIndex, std::move(frame.changes));
    }
    const std::size_t changed = commitAll(context, std::move(changes));
    showStatus(tr("Scrolling fill: %1 updated").arg(framesText(changed)), kStatusMs);
}

void EditHelpersController::recolor(const CanvasEditContext& context,
                                    const render::CanvasHit& hit)
{
    if (!context.document || !context.currentFrame || !editsFramePixels(context) ||
        hit.region != render::CanvasRegion::ColorizedFrame) {
        return;
    }
    const document::ProjectDocument& document = *context.document;
    try {
        if (edit::isDynamicPixel(document, *context.currentFrame, hit.resolution,
                                 render::Point{hit.x, hit.y})) {
            showStatus(tr("The pixel is dynamically colored; recolor works on static "
                          "content only"),
                       kLongStatusMs);
            return;
        }
        const uint16_t color =
            document.colorizedPixel(*context.currentFrame, hit.resolution, hit.x, hit.y);
        std::vector<std::pair<std::size_t, edit::FrameContentChanges>> changes;
        for (std::size_t frameIndex : context.selectedFrames) {
            if (hit.resolution == render::Resolution::Hd && !document.hasHdFrame(frameIndex)) {
                continue;
            }
            const uint16_t drawColor = context.drawColor;
            const edit::PixelColorSource source =
                context.drawColorMode == edit::DrawColorMode::ColorSetReplace
                    ? edit::colorSetReplaceColorSource(document, frameIndex, hit.resolution)
                    : edit::PixelColorSource([drawColor](render::Point) { return drawColor; });
            changes.emplace_back(frameIndex,
                                 edit::makeRecolorChanges(document, frameIndex, hit.resolution,
                                                          color, source));
        }
        const std::size_t changed = commitAll(context, std::move(changes));
        showStatus(tr("Recolored the pixels of color 0x%1 on %2")
                       .arg(color, 4, 16, QLatin1Char('0'))
                       .arg(framesText(changed)),
                   kStatusMs);
    } catch (const std::exception& ex) {
        showStatus(QString::fromUtf8(ex.what()), kLongStatusMs);
    }
}

void EditHelpersController::blackOriginalToBlack(const CanvasEditContext& context,
                                                 const edit::PlaneSelection* selection)
{
    if (!context.document || !context.currentFrame || !editsFramePixels(context)) {
        showStatus(tr("Black original to black works on the frame's pixels"),
                   kStatusMs);
        return;
    }
    const edit::PlaneSelection* area = areaOn(selection, context.activePlane);
    std::vector<std::pair<std::size_t, edit::FrameContentChanges>> changes;
    try {
        for (std::size_t frameIndex : context.selectedFrames) {
            changes.emplace_back(frameIndex,
                                 edit::makeBlackOriginalToBlackChanges(*context.document,
                                                                       frameIndex,
                                                                       context.activePlane,
                                                                       area));
        }
    } catch (const std::exception& ex) {
        showStatus(QString::fromUtf8(ex.what()), kLongStatusMs);
        return;
    }
    const std::size_t changed = commitAll(context, std::move(changes));
    showStatus((area ? tr("Black original to black in the selection: %1 changed")
                     : tr("Black original to black: %1 changed"))
                   .arg(framesText(changed)),
               kStatusMs);
}

bool EditHelpersController::requireDynamicPlane(const CanvasEditContext& context) const
{
    if (!context.document || !context.currentFrame || !editsFramePixels(context)) {
        showStatus(tr("The dynamic helpers work on the frame's pixels"), kStatusMs);
        return false;
    }
    const render::Resolution plane = context.document->dynamicPlane();
    if (context.activePlane != plane) {
        const render::Size size = context.document->geometry().planeSize(plane);
        showStatus(tr("Dynamic zones are defined on the %1x%2 plane only")
                       .arg(size.width)
                       .arg(size.height),
                   kLongStatusMs);
        return false;
    }
    return true;
}

void EditHelpersController::removeDynamicContent(const CanvasEditContext& context,
                                                 const edit::PlaneSelection* selection)
{
    if (!requireDynamicPlane(context)) {
        return;
    }
    const edit::PlaneSelection* area = areaOn(selection, context.activePlane);
    std::vector<std::pair<std::size_t, edit::FrameContentChanges>> changes;
    try {
        for (std::size_t frameIndex : context.selectedFrames) {
            changes.emplace_back(frameIndex,
                                 edit::makeRemoveDynamicChanges(*context.document, frameIndex,
                                                                area));
        }
    } catch (const std::exception& ex) {
        showStatus(QString::fromUtf8(ex.what()), kLongStatusMs);
        return;
    }
    const std::size_t changed = commitAll(context, std::move(changes));
    showStatus((area ? tr("Removed the dynamic zones in the selection: %1 changed")
                     : tr("Removed the dynamic zones: %1 changed"))
                   .arg(framesText(changed)),
               kStatusMs);
}

void EditHelpersController::dynamicToStatic(const CanvasEditContext& context,
                                            const edit::PlaneSelection* selection)
{
    if (!requireDynamicPlane(context)) {
        return;
    }
    const edit::PlaneSelection* area = areaOn(selection, context.activePlane);
    std::vector<std::pair<std::size_t, edit::FrameContentChanges>> changes;
    try {
        for (std::size_t frameIndex : context.selectedFrames) {
            changes.emplace_back(
                frameIndex, edit::makeDynamicToStaticChanges(*context.document, frameIndex, area));
        }
    } catch (const std::exception& ex) {
        showStatus(QString::fromUtf8(ex.what()), kLongStatusMs);
        return;
    }
    const std::size_t changed = commitAll(context, std::move(changes));
    showStatus((area ? tr("Made the dynamic content static in the selection: %1 changed")
                     : tr("Made the dynamic content static: %1 changed"))
                   .arg(framesText(changed)),
               kStatusMs);
}

void EditHelpersController::brightness(const CanvasEditContext& context,
                                       const edit::PlaneSelection* selection,
                                       double coefficient)
{
    const edit::PlaneSelection* area = areaOn(selection, context.activePlane);
    if (!context.document || !area) {
        showStatus(tr("Brightness works on the selected pixels: select them first"),
                   kLongStatusMs);
        return;
    }
    std::vector<std::pair<std::size_t, edit::FrameContentChanges>> changes;
    try {
        for (std::size_t frameIndex : context.selectedFrames) {
            changes.emplace_back(frameIndex,
                                 edit::makeBrightnessChanges(*context.document,
                                                             frameIndex,
                                                             context.activePlane,
                                                             *area,
                                                             coefficient));
        }
    } catch (const std::exception& ex) {
        showStatus(QString::fromUtf8(ex.what()), kLongStatusMs);
        return;
    }
    const std::size_t changed = commitAll(context, std::move(changes));
    showStatus(tr("Brightness %1%: %2 changed")
                   .arg(qRound(coefficient * 100))
                   .arg(framesText(changed)),
               kStatusMs);
}

void EditHelpersController::shadowSelection(const CanvasEditContext& context,
                                            const edit::PlaneSelection* selection,
                                            edit::SelectionDirection direction)
{
    const edit::PlaneSelection* area = areaOn(selection, context.activePlane);
    if (!context.document || !area) {
        showStatus(tr("A shadow goes around the selection: select first"),
                   kLongStatusMs);
        return;
    }
    std::vector<std::pair<std::size_t, edit::FrameContentChanges>> changes;
    try {
        for (std::size_t frameIndex : context.selectedFrames) {
            changes.emplace_back(frameIndex,
                                 edit::makeSelectionShadowChanges(*context.document,
                                                                  frameIndex,
                                                                  context.activePlane,
                                                                  *area,
                                                                  direction,
                                                                  context.drawColor));
        }
    } catch (const std::exception& ex) {
        showStatus(QString::fromUtf8(ex.what()), kLongStatusMs);
        return;
    }
    const std::size_t changed = commitAll(context, std::move(changes));
    showStatus(tr("Drew a shadow next to the selection: %1 changed")
                   .arg(framesText(changed)),
               kStatusMs);
}

void EditHelpersController::assignDynamicSet(const CanvasEditContext& context,
                                             const edit::PlaneSelection* selection)
{
    if (!requireDynamicPlane(context)) {
        return;
    }
    const edit::PlaneSelection* area = areaOn(selection, context.activePlane);
    std::vector<std::pair<std::size_t, edit::FrameContentChanges>> changes;
    try {
        for (std::size_t frameIndex : context.selectedFrames) {
            changes.emplace_back(frameIndex,
                                 edit::makeAssignDynamicSetChanges(*context.document,
                                                                   frameIndex,
                                                                   area,
                                                                   context.dynamicSet));
        }
    } catch (const std::exception& ex) {
        showStatus(QString::fromUtf8(ex.what()), kLongStatusMs);
        return;
    }
    const std::size_t changed = commitAll(context, std::move(changes));
    showStatus((area ? tr("Dynamic set %1 assigned to the selection: %2 changed")
                     : tr("Dynamic set %1 assigned: %2 changed"))
                   .arg(static_cast<int>(context.dynamicSet))
                   .arg(framesText(changed)),
               kStatusMs);
}

std::size_t EditHelpersController::commitAll(
    const CanvasEditContext& context,
    std::vector<std::pair<std::size_t, edit::FrameContentChanges>> changes)
{
    std::size_t changed = 0;
    const render::Resolution dynamicPlane = context.document->dynamicPlane();
    for (auto& [frameIndex, frameChanges] : changes) {
        if (frameChanges.isEmpty()) {
            continue;
        }
        if (m_outputs.commit) {
            m_outputs.commit(std::make_unique<edit::FrameContentCommand>(
                frameIndex, dynamicPlane, std::move(frameChanges), historyKind(context)));
        }
        ++changed;
    }
    if (changed > 0 && m_outputs.commitsFinished) {
        m_outputs.commitsFinished();
    }
    return changed;
}

void EditHelpersController::showStatus(const QString& text, int timeoutMs) const
{
    if (m_outputs.showStatus) {
        m_outputs.showStatus(text, timeoutMs);
    }
}

}  // namespace colorizing::app

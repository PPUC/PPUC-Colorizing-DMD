#include "EditHelpersController.h"
#include "EditMenuActions.h"
#include "PasteModeController.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>

#include "CanvasEditController.h"
#include "FramePreviewWidget.h"
#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/FrameContentCommand.h"
#include "edit/UndoHistories.h"

using colorizing::app::CanvasEditContext;
using colorizing::app::CanvasEditController;
using colorizing::app::CanvasEditLayer;
using colorizing::app::CanvasEditOutputs;
using colorizing::app::CanvasPasteOverlay;
using colorizing::app::CanvasPixelOverlay;
using colorizing::app::EditHelpersController;
using colorizing::app::EditHelpersOutputs;
using colorizing::app::EditMenuActions;
using colorizing::app::PasteModeController;
using colorizing::app::PasteModeOutputs;
using colorizing::document::ProjectDocument;
using colorizing::edit::DrawingTool;
using colorizing::render::CanvasHit;
using colorizing::render::CanvasRegion;
using colorizing::render::PlaneLayout;
using colorizing::render::Point;
using colorizing::render::ProjectGeometry;
using colorizing::render::Resolution;
using colorizing::render::Size;

namespace document = colorizing::document;
namespace edit = colorizing::edit;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: selection widgets: " << message << '\n';
        std::exit(1);
    }
}

// Three frames on a flat original; frame k is colored 0x100 * (k + 1) everywhere, with a
// square of 0xf800 at 10..12, 5..7 in frame 0, and HD pictures for frames 0 and 2.
ProjectDocument makeDocument(PlaneLayout layout = PlaneLayout::Ordinary)
{
    const ProjectGeometry geometry(layout);
    document::ProjectSnapshot snapshot;
    snapshot.romName = "selection";
    snapshot.colorCount = 4;
    snapshot.geometry = geometry;
    for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
        document::IndexedFrame original = document::makeIndexedFrame(geometry.romFrameSize(), 1);
        original.pixels[0] = 0;
        document::ProjectFrameSnapshot frame =
            document::makeUncolorizedFrameSnapshot(geometry, 4, original, 30);
        frame.colorizedSd.pixels.assign(frame.colorizedSd.pixels.size(),
                                        static_cast<uint16_t>(0x100 * (frameIndex + 1)));
        if (frame.colorizedHd) {
            frame.colorizedHd->pixels.assign(frame.colorizedHd->pixels.size(),
                                             static_cast<uint16_t>(0x1000 * (frameIndex + 1)));
        }
        if (layout == PlaneLayout::Ordinary && frameIndex != 1) {
            frame.colorizedHd = document::makeRgb565Frame(geometry.planeSize(Resolution::Hd),
                                                          static_cast<uint16_t>(0x40 + frameIndex));
        }
        snapshot.frames.push_back(frame);
    }
    const Resolution native = geometry.nativePlane();
    auto& first = native == Resolution::Hd ? *snapshot.frames[0].colorizedHd
                                           : snapshot.frames[0].colorizedSd;
    for (int y = 5; y <= 7; ++y) {
        for (int x = 10; x <= 12; ++x) {
            first.pixels[static_cast<std::size_t>(y * first.size.width + x)] = 0xf800;
        }
    }
    snapshot.palettes.assign(document::kPaletteCount,
                             document::PaletteRecord{std::vector<uint16_t>(64, 0), ""});
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();
    return document;
}

CanvasEditContext makeContext(const ProjectDocument& document,
                              std::vector<std::size_t> selectedFrames,
                              DrawingTool tool = DrawingTool::Point)
{
    CanvasEditContext context;
    context.document = &document;
    context.currentFrame = selectedFrames.front();
    context.selectedFrames = std::move(selectedFrames);
    context.tool = tool;
    context.drawColor = 0x07e0;
    context.activePlane = document.geometry().nativePlane();
    return context;
}

CanvasHit hitAt(Resolution plane, int x, int y)
{
    return CanvasHit{CanvasRegion::ColorizedFrame, plane, x, y};
}

struct CanvasOwner {
    CanvasEditOutputs outputs()
    {
        CanvasEditOutputs out;
        out.showStatus = [this](const QString& text, int) { status = text.toStdString(); };
        out.showSelectionOverlay = [this](CanvasPixelOverlay overlay) {
            overlayPixels = overlay.pixels.size();
        };
        out.clearSelectionOverlay = [this]() { overlayPixels = 0; };
        out.selectionChanged = [this]() { ++changes; };
        out.commit = [this](std::unique_ptr<edit::EditCommand> command) {
            command->apply(*document);
            ++commits;
        };
        return out;
    }

    ProjectDocument* document = nullptr;
    std::string status;
    std::size_t overlayPixels = 0;
    int changes = 0;
    int commits = 0;
};

void testSelectionEditing()
{
    ProjectDocument document = makeDocument();
    CanvasOwner owner;
    owner.document = &document;
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context = makeContext(document, {0});

    // Ctrl+click with the point tool adds pixels, with Shift removes them.
    controller.editSelectionAtPoint(context, hitAt(Resolution::Sd, 3, 4), false, false);
    controller.editSelectionAtPoint(context, hitAt(Resolution::Sd, 4, 4), false, false);
    expect(controller.selection() && controller.selection()->count() == 2 &&
               owner.overlayPixels == 2,
           "points join the selection and the overlay shows them");
    controller.editSelectionAtPoint(context, hitAt(Resolution::Sd, 3, 4), true, false);
    expect(controller.selection()->count() == 1, "Shift removes a point");

    // Ctrl+drag with a filled rectangle adds its pixels; with Shift removes them.
    context.tool = DrawingTool::FilledRectangle;
    controller.editSelectionWithStroke(context, hitAt(Resolution::Sd, 0, 0),
                                       hitAt(Resolution::Sd, 3, 1), false, false);
    expect(controller.selection()->count() == 9, "a filled rectangle joins the selection");
    controller.editSelectionWithStroke(context, hitAt(Resolution::Sd, 0, 0),
                                       hitAt(Resolution::Sd, 1, 1), true, false);
    expect(controller.selection()->count() == 5, "a filled rectangle with Shift leaves it");

    // Only non-black pixels: pixel 0,0 of frame 0 becomes black.
    document.setColorizedPixel(0, Resolution::Sd, 0, 0, 0);
    controller.editSelectionWithStroke(context, hitAt(Resolution::Sd, 0, 0),
                                       hitAt(Resolution::Sd, 1, 1), false, true);
    expect(controller.selection()->count() == 8 &&
               !controller.selection()->contains(Point{0, 0}),
           "black pixels stay out with nonBlackOnly");

    // The fill tool with Ctrl selects the connected pixels of the clicked color.
    context.tool = DrawingTool::FloodFill;
    controller.clearSelection(context);
    controller.editSelectionAtPoint(context, hitAt(Resolution::Sd, 11, 6), false, false);
    expect(controller.selection()->count() == 9, "Ctrl+fill selects the red square");
    expect(controller.hasSelection(), "hasSelection follows");

    // Invert, mirror, select all and none.
    context.tool = DrawingTool::Point;
    controller.invertSelection(context);
    expect(controller.selection()->count() == 128u * 32u - 9u, "invert");
    controller.invertSelection(context);
    controller.mirrorSelection(context, true, false);
    expect(controller.selection()->contains(Point{127 - 11, 6}), "mirror horizontally");
    controller.mirrorSelection(context, false, true);
    expect(controller.selection()->contains(Point{127 - 11, 31 - 6}), "mirror vertically");
    controller.selectAll(context);
    expect(controller.selection()->count() == 128u * 32u, "select all");
    controller.clearSelection(context);
    expect(!controller.selection() && owner.overlayPixels == 0, "select none");

    // Select by color: the composed picture, every pixel of the color.
    controller.selectByColor(context, hitAt(Resolution::Sd, 11, 6), false);
    expect(controller.selection() && controller.selection()->count() == 9, "select by color");
    controller.selectByColor(context, hitAt(Resolution::Sd, 50, 20), false);
    expect(controller.selection()->count() == 128u * 32u - 1u,
           "adding the other color selects all but the black pixel");
    controller.selectByColor(context, hitAt(Resolution::Sd, 11, 6), true);
    expect(controller.selection()->count() == 128u * 32u - 10u, "Shift unselects a color");

    // Stripes, and the background mask of a frame that has a background.
    controller.selectStripes(context, edit::StripePattern{0, 1, 3, false});
    expect(controller.selection()->count() == 8u * 128u, "every fourth row");
    controller.selectBackgroundMask(context);
    expect(owner.status.find("no background") != std::string::npos,
           "a frame without background says so");

    // A selection on the HD plane replaces the SD one; Grad Sel uses the new selection.
    context.activePlane = Resolution::Hd;
    controller.selectStripes(context, edit::StripePattern{0, 2, 62, false});
    expect(controller.selection()->plane() == Resolution::Hd &&
               controller.selection()->count() == 2u * 256u,
           "an HD selection");
    context.selectedPalette = 0;
    document::PaletteRecord palette = document.palette(0);
    for (std::size_t color = 0; color < 64; ++color) {
        document.setPaletteColor(0, color, static_cast<uint16_t>(0x2000 + color));
    }
    context.gradientEndpoints = []() { return std::pair<int, int>{0, 7}; };
    controller.fillSelectionWithPaletteGradient(context);
    expect(owner.commits == 1 && document.colorizedPixel(0, Resolution::Hd, 0, 0) == 0x2000 &&
               document.colorizedPixel(0, Resolution::Hd, 255, 1) == 0x2007 &&
               document.colorizedPixel(0, Resolution::Hd, 0, 2) == 0x40,
           "Grad Sel fills the edited selection over its bounds");
    (void)palette;

    // The Dynamic and BG modes select on the frame's picture as without them (Ctrl+click,
    // Ctrl+A, the Sel tool); comparison masks, painted on the original, have no selection.
    context.layer = CanvasEditLayer::DynamicMask;
    expect(CanvasEditController::canEditSelection(context), "selecting in the Dynamic mode");
    context.activePlane = Resolution::Sd;
    controller.selectAll(context);
    expect(controller.selection() && controller.selection()->plane() == Resolution::Sd &&
               controller.selection()->count() == 128u * 32u,
           "Select All in the Dynamic mode");
    context.layer = CanvasEditLayer::BackgroundMask;
    expect(CanvasEditController::canEditSelection(context), "and in the BG mode");
    context.layer = CanvasEditLayer::ComparisonMask;
    expect(!CanvasEditController::canEditSelection(context), "not on a comparison mask");
}

struct PasteOwner {
    PasteModeOutputs outputs()
    {
        PasteModeOutputs out;
        out.commit = [this](std::unique_ptr<edit::EditCommand> command) {
            command->apply(*document);
            const edit::HistoryKey key = edit::historyKey(command->target());
            keys.push_back(key);
            histories.push(key, std::move(command));
        };
        out.commitsFinished = [this]() { ++finished; };
        out.showStatus = [this](const QString& text, int) { status = text.toStdString(); };
        out.showOverlay = [this](std::optional<CanvasPasteOverlay> shown) {
            overlay = std::move(shown);
        };
        out.activeChanged = [this](bool active) { activeChanges.push_back(active); };
        out.exportImage = [this](const QImage& image) { exported = image; };
        return out;
    }

    ProjectDocument* document = nullptr;
    edit::UndoHistories histories;
    std::vector<edit::HistoryKey> keys;
    int finished = 0;
    std::string status;
    std::optional<CanvasPasteOverlay> overlay;
    std::vector<bool> activeChanges;
    QImage exported;
};

void testPasteMode()
{
    ProjectDocument document = makeDocument();
    PasteOwner owner;
    owner.document = &document;
    PasteModeController paste(owner.outputs());
    CanvasEditContext context = makeContext(document, {0});

    expect(!paste.begin(context, edit::PasteMode::All) && !paste.isActive() &&
               owner.status == "Nothing has been copied",
           "paste needs something copied");
    expect(!paste.copy(context, nullptr), "copy needs a selection");

    edit::PlaneSelection selection(Resolution::Sd, Size{128, 32});
    selection.add({Point{10, 5}, Point{11, 5}, Point{12, 5}, Point{10, 6}});
    expect(paste.copy(context, &selection) && paste.clipboard() &&
               paste.clipboard()->selectedCount() == 4,
           "copy fills the clipboard");
    expect(owner.exported.width() == 3 && owner.exported.height() == 2 &&
               owner.exported.pixelColor(0, 0).alpha() == 255 &&
               owner.exported.pixelColor(1, 1).alpha() == 0,
           "the copied colors go to the system clipboard as an image");

    // Begin: the box where it was copied from, the overlay shows it.
    context = makeContext(document, {1, 2});
    context.currentFrame = 1;
    expect(paste.begin(context, edit::PasteMode::All) && paste.isActive() &&
               owner.activeChanges == std::vector<bool>{true},
           "paste mode starts");
    expect(owner.overlay && owner.overlay->box.x == 10 && owner.overlay->box.y == 5 &&
               owner.overlay->pixels.size() == 4,
           "the overlay is at the copied place");

    // Follow the mouse: the box is centred on the pixel.
    paste.follow(hitAt(Resolution::Sd, 40, 20));
    expect(paste.placement().x == 39 && paste.placement().y == 19, "the box follows the mouse");
    paste.follow(hitAt(Resolution::Hd, 1, 1));
    expect(paste.placement().x == 39, "a hit on another plane is ignored");

    // Keys: arrows, Shift+arrow, keypad, mirror.
    expect(paste.handleKey(context, Qt::Key_Right, Qt::NoModifier) && paste.placement().x == 40,
           "arrow right");
    expect(paste.handleKey(context, Qt::Key_Up, Qt::ShiftModifier) && paste.placement().y == 9,
           "Shift moves by 10");
    expect(paste.handleKey(context, Qt::Key_2, Qt::KeypadModifier) && paste.placement().y == 10,
           "keypad 2 moves down as in Win32");
    expect(!paste.handleKey(context, Qt::Key_2, Qt::NoModifier), "a plain 2 is not taken");
    expect(paste.handleKey(context, Qt::Key_K, Qt::NoModifier) &&
               paste.placement().mirrorHorizontally,
           "K mirrors horizontally");
    bool mirroredCorner = false;
    bool unmirroredCorner = false;
    for (const auto& [pixel, color] : owner.overlay->pixels) {
        mirroredCorner = mirroredCorner || (pixel.x == 42 && pixel.y == 11);
        unmirroredCorner = unmirroredCorner || (pixel.x == 40 && pixel.y == 11);
    }
    expect(mirroredCorner && !unmirroredCorner, "the overlay is mirrored");
    paste.handleKey(context, Qt::Key_K, Qt::NoModifier);
    expect(!paste.placement().mirrorHorizontally, "K again turns it back");

    // Stamp with Enter on both selected frames: one command per frame, paste mode ends.
    const std::string before = [&document]() {
        std::string pixels;
        for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
            for (uint16_t value : document.colorizedFrame(frame, Resolution::Sd).pixels) {
                pixels += std::to_string(value) + ',';
            }
        }
        return pixels;
    }();
    expect(paste.handleKey(context, Qt::Key_Return, Qt::NoModifier), "Enter stamps");
    expect(!paste.isActive() && !owner.overlay && owner.keys.size() == 2 && owner.finished == 1,
           "one step per frame, then paste mode ends");
    expect(document.colorizedPixel(1, Resolution::Sd, 40, 10) == 0xf800 &&
               document.colorizedPixel(2, Resolution::Sd, 42, 10) == 0xf800 &&
               document.colorizedPixel(2, Resolution::Sd, 40, 11) == 0xf800 &&
               document.colorizedPixel(2, Resolution::Sd, 41, 11) == 0x300,
           "the copied pixels landed on both frames");
    for (const edit::HistoryKey& key : owner.keys) {
        owner.histories.undo(key, document);
    }
    std::string after;
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        for (uint16_t value : document.colorizedFrame(frame, Resolution::Sd).pixels) {
            after += std::to_string(value) + ',';
        }
    }
    expect(after == before, "undo frame by frame restores the frames");

    // Escape cancels without a change; Shift and Alt pick the variants.
    paste.begin(context, edit::PasteMode::All);
    expect(paste.stampMode(Qt::ShiftModifier) == edit::PasteMode::WhereOriginalsMatch &&
               paste.stampMode(Qt::AltModifier) == edit::PasteMode::DynamicMaskOnly &&
               paste.stampMode(Qt::NoModifier) == edit::PasteMode::All,
           "stamp modifiers");
    owner.keys.clear();
    expect(paste.handleKey(context, Qt::Key_Escape, Qt::NoModifier) && !paste.isActive() &&
               owner.keys.empty(),
           "Escape cancels");
    expect(!paste.handleKey(context, Qt::Key_Escape, Qt::NoModifier),
           "keys are not taken outside paste mode");

    // A click stamps where the mouse is, with the mode's variant.
    paste.begin(context, edit::PasteMode::WhereOriginalsMatch);
    paste.follow(hitAt(Resolution::Sd, 1, 1));
    expect(paste.stamp(context, Qt::NoModifier), "a click stamps");
    expect(document.colorizedPixel(1, Resolution::Sd, 0, 0) == 0x200,
           "pixel 0,0 has another original shade and is left alone");
    expect(document.colorizedPixel(1, Resolution::Sd, 1, 0) == 0xf800, "matching pixels change");
    expect(!paste.isActive() && !paste.staysActive(), "as in Win32 the stamp ends paste mode");

    // Stay In Paste Mode: stamps go on until Escape.
    paste.setStaysActive(true);
    paste.begin(context, edit::PasteMode::All);
    paste.follow(hitAt(Resolution::Sd, 1, 1));
    owner.keys.clear();
    expect(paste.stamp(context, Qt::NoModifier) && paste.isActive() &&
               owner.status.find("Esc ends it") != std::string::npos,
           "with Stay In Paste Mode the stamp keeps paste mode and says how to end it");
    const std::size_t steps = owner.keys.size();
    paste.follow(hitAt(Resolution::Sd, 60, 20));
    expect(paste.stamp(context, Qt::NoModifier) && paste.isActive() && owner.keys.size() > steps,
           "the paste is stamped again elsewhere, with undo steps of its own");
    expect(paste.handleKey(context, Qt::Key_Escape, Qt::NoModifier) && !paste.isActive(),
           "Escape ends it");
    paste.setStaysActive(false);

    // On the HD plane the SD content is refused with a message.
    context.activePlane = Resolution::Hd;
    expect(!paste.begin(context, edit::PasteMode::All) &&
               owner.status.find("128x32 plane") != std::string::npos,
           "SD content cannot be pasted on the HD plane");
    context.activePlane = Resolution::Sd;
    context.layer = CanvasEditLayer::BackgroundImage;
    expect(!paste.begin(context, edit::PasteMode::All), "not on a background");
    context.layer = CanvasEditLayer::DynamicMask;
    expect(paste.begin(context, edit::PasteMode::All), "in Dynamic mode");
    owner.keys.clear();
    paste.stamp(context, Qt::NoModifier);
    expect(!owner.keys.empty() && owner.keys.front().kind == edit::EditKind::DynamicMask,
           "a paste in Dynamic mode is undone there");

    paste.clearClipboard();
    expect(!paste.clipboard(), "clearing the clipboard");
}

void testHelpers()
{
    ProjectDocument document = makeDocument();
    std::vector<std::unique_ptr<edit::EditCommand>> committed;
    std::string status;
    EditHelpersOutputs outputs;
    outputs.commit = [&](std::unique_ptr<edit::EditCommand> command) {
        command->apply(document);
        committed.push_back(std::move(command));
    };
    outputs.showStatus = [&](const QString& text, int) { status = text.toStdString(); };
    EditHelpersController helpers(outputs);
    CanvasEditContext context = makeContext(document, {0, 1, 2}, DrawingTool::FloodFill);

    // Recolor: every pixel of 0x100 in frame 0 (the clicked frame's color) on each frame.
    helpers.recolor(context, hitAt(Resolution::Sd, 50, 20));
    expect(committed.size() == 1 && document.colorizedPixel(0, Resolution::Sd, 50, 20) == 0x07e0 &&
               document.colorizedPixel(0, Resolution::Sd, 11, 6) == 0xf800,
           "recolor changes the clicked color only, on the frames that have it");

    // Black original to black on the HD plane: frames 0 and 2 have HD.
    committed.clear();
    context.activePlane = Resolution::Hd;
    helpers.blackOriginalToBlack(context, nullptr);
    expect(committed.size() == 2 && document.colorizedPixel(0, Resolution::Hd, 1, 1) == 0 &&
               document.colorizedPixel(0, Resolution::Hd, 2, 1) == 0x40,
           "the 2x2 block of the black ROM pixel");

    // The dynamic helpers need the dynamic plane.
    committed.clear();
    helpers.assignDynamicSet(context, nullptr);
    expect(committed.empty() && status.find("128x32 plane") != std::string::npos,
           "no dynamic helper on the HD plane of an ordinary project");
    context.activePlane = Resolution::Sd;
    context.dynamicSet = 4;
    edit::PlaneSelection area(Resolution::Sd, Size{128, 32});
    area.add({Point{1, 1}, Point{2, 1}});
    helpers.assignDynamicSet(context, &area);
    expect(committed.size() == 3 && document.dynamicMaskPixel(2, Resolution::Sd, 2, 1) == 4 &&
               document.dynamicMaskPixel(2, Resolution::Sd, 3, 1) == 255,
           "assign the dynamic set to the selection on every frame");
    committed.clear();
    helpers.removeDynamicContent(context, nullptr);
    expect(committed.size() == 3 && document.dynamicMaskPixel(2, Resolution::Sd, 2, 1) == 255,
           "remove the dynamic zones of the frames");
    expect(document.colorizedPixel(2, Resolution::Sd, 2, 1) ==
               document::plainOriginalColors(4)[1],
           "removed dynamic pixels get their plain color");

    // Auto copy: frames 1 and 2 share frame 0's original, so all match.
    edit::PlaneSelection square(Resolution::Sd, Size{128, 32});
    square.add({Point{10, 5}, Point{11, 5}});
    expect(EditHelpersController::autoCopyRefusal(context, nullptr).size() > 0,
           "auto copy needs a selection");
    const colorizing::app::AutoCopyPlan plan = helpers.planAutoCopy(context, square, {});
    expect(plan.scan.matches == std::vector<std::size_t>{1, 2} && plan.changingFrames.size() == 2,
           "auto copy plan");
    committed.clear();
    helpers.applyAutoCopy(context, square, plan);
    expect(committed.size() == 2 && document.colorizedPixel(2, Resolution::Sd, 11, 5) == 0xf800,
           "auto copy applied");

    // Scrolling fill: the run of selected frames around the current one.
    const colorizing::app::ScrollFillPlan scroll = helpers.planScrollFill(context);
    expect(scroll.firstFrame == 0 && scroll.lastFrame == 2 && scroll.frames.size() == 1,
           "the scroll run");
}

void testPreviewInterceptAndHover()
{
    ProjectDocument document = makeDocument();
    colorizing::app::FramePreviewWidget widget;
    widget.resize(640, 420);
    widget.setDocument(&document);
    widget.setActiveResolution(Resolution::Sd);
    widget.setFrameIndex(0);

    int clicks = 0;
    int finished = 0;
    widget.setPixelClickedHandler([&](CanvasHit, bool) { ++clicks; });
    widget.setStrokeFinishedHandler([&](CanvasHit, CanvasHit, bool) { ++finished; });
    std::optional<Qt::KeyboardModifiers> intercepted;
    widget.setPressInterceptor([&](CanvasHit, Qt::KeyboardModifiers modifiers) {
        intercepted = modifiers;
        return true;
    });
    const auto press = [&](Qt::KeyboardModifiers modifiers) {
        QMouseEvent down(QEvent::MouseButtonPress, QPointF(90, 90), QPointF(90, 90),
                         QPointF(90, 90), Qt::LeftButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(&widget, &down);
        QMouseEvent up(QEvent::MouseButtonRelease, QPointF(90, 90), QPointF(90, 90),
                       QPointF(90, 90), Qt::LeftButton, Qt::NoButton, modifiers);
        QApplication::sendEvent(&widget, &up);
    };
    press(Qt::ShiftModifier | Qt::AltModifier);
    expect(intercepted && intercepted->testFlag(Qt::AltModifier) && clicks == 0 && finished == 0,
           "an intercepted press makes no click and no stroke");
    widget.setPressInterceptor(nullptr);
    press(Qt::ControlModifier);
    expect(clicks == 1 && finished == 1 && widget.pressModifiers().testFlag(Qt::ControlModifier),
           "the modifiers of the press are kept for its handlers");

    std::optional<CanvasHit> hovered;
    widget.setHoverHandler([&](CanvasHit hit) { hovered = hit; });
    expect(widget.hasMouseTracking(), "a hover handler tracks the mouse");
    QMouseEvent move(QEvent::MouseMove, QPointF(100, 90), QPointF(100, 90), QPointF(100, 90),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &move);
    expect(hovered && hovered->region == CanvasRegion::ColorizedFrame, "hover reports the pixel");
    widget.setHoverHandler(nullptr);
    expect(!widget.hasMouseTracking(), "no tracking without a handler");

    CanvasPasteOverlay overlay;
    overlay.box = colorizing::render::Rect{1, 1, 2, 2};
    overlay.pixels.emplace_back(Point{1, 1}, QColor(255, 0, 0));
    widget.setPasteOverlay(overlay);
    const QImage withOverlay = widget.grab().toImage();
    widget.setPasteOverlay(std::nullopt);
    expect(widget.grab().toImage() != withOverlay, "the paste overlay is drawn");
}

void testMenuActions()
{
    QMainWindow window;
    QMenu* menu = window.menuBar()->addMenu(QStringLiteral("Edit"));
    EditMenuActions actions(menu, &window);
    QStringList calls;
    actions.onCopy = [&]() { calls << QStringLiteral("copy"); };
    actions.onPaste = [&]() { calls << QStringLiteral("paste"); };
    actions.onSelectAll = [&]() { calls << QStringLiteral("all"); };
    actions.onSelectNone = [&]() { calls << QStringLiteral("none"); };
    actions.onMirrorHorizontally = [&]() { calls << QStringLiteral("mirror"); };

    const auto find = [&](const QString& prefix) -> QAction* {
        for (QAction* action : actions.actions()) {
            if (action->text().remove(QLatin1Char('&')).startsWith(prefix)) {
                return action;
            }
        }
        expect(false, "no action " + prefix.toStdString());
        return nullptr;
    };
    expect(find(QStringLiteral("Copy"))->shortcut() == QKeySequence(QKeySequence::Copy) &&
               find(QStringLiteral("Paste"))->shortcut() == QKeySequence(QKeySequence::Paste) &&
               find(QStringLiteral("Select All"))->shortcut() ==
                   QKeySequence(QKeySequence::SelectAll),
           "standard shortcuts");
    expect(find(QStringLiteral("Select None"))->shortcuts().size() == 2 &&
               find(QStringLiteral("Invert Selection"))->shortcut() ==
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I) &&
               find(QStringLiteral("Black Original To Black"))->shortcut() ==
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O) &&
               find(QStringLiteral("Remove Dynamic Zones"))->shortcuts().size() == 2 &&
               find(QStringLiteral("Assign Dynamic Set"))->shortcut() ==
                   QKeySequence(Qt::ALT | Qt::Key_A),
           "the other shortcuts");

    // No two actions of the window share a shortcut.
    QList<QKeySequence> seen;
    for (QAction* action : window.findChildren<QAction*>()) {
        for (const QKeySequence& sequence : action->shortcuts()) {
            expect(!seen.contains(sequence), "duplicate shortcut " +
                                                 sequence.toString().toStdString());
            seen << sequence;
        }
    }

    expect(!find(QStringLiteral("Copy"))->isEnabled(), "disabled without a project");
    actions.setState(true, false);
    expect(find(QStringLiteral("Copy"))->isEnabled() &&
               !find(QStringLiteral("Paste Where Originals Match"))->isEnabled(),
           "paste variants need the clipboard");
    actions.setState(true, true);
    expect(find(QStringLiteral("Paste Dynamic Mask Only"))->isEnabled(), "with the clipboard");

    // A focused line edit keeps the standard keys; the canvas gets them otherwise.
    window.show();
    auto* lineEdit = new QLineEdit(&window);
    window.setCentralWidget(lineEdit);
    lineEdit->setText(QStringLiteral("abc"));
    lineEdit->setFocus();
    QApplication::processEvents();
    expect(EditMenuActions::forwardStandardKey(lineEdit, QKeySequence::SelectAll) &&
               lineEdit->selectedText() == QStringLiteral("abc"),
           "Select All selects the text of a line edit");
    QListWidget list;
    list.setSelectionMode(QAbstractItemView::ExtendedSelection);
    list.addItems({QStringLiteral("a"), QStringLiteral("b")});
    expect(EditMenuActions::forwardStandardKey(&list, QKeySequence::SelectAll) &&
               list.selectedItems().size() == 2,
           "Select All selects every row of a list");
    expect(!EditMenuActions::forwardStandardKey(&list, QKeySequence::Copy),
           "Copy in a list copies the canvas selection");
    expect(!EditMenuActions::forwardStandardKey(nullptr, QKeySequence::Copy), "no focus");

    // Triggering an action without a text field in focus calls the handler.
    lineEdit->clearFocus();
    window.setFocus();
    QApplication::processEvents();
    find(QStringLiteral("Mirror Selection Horizontally"))->trigger();
    find(QStringLiteral("Select None"))->trigger();
    expect(calls == QStringList({QStringLiteral("mirror"), QStringLiteral("none")}),
           "handlers are called");
}

}  // namespace

void runSelectionWidgetTests()
{
    testSelectionEditing();
    testPasteMode();
    testHelpers();
    testPreviewInterceptAndHover();
    testMenuActions();
}

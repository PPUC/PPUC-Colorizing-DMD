#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolButton>

#include "CanvasEditController.h"
#include "EditColorKeys.h"
#include "FrameImageUtils.h"
#include "dialogs/ColorPickerDialog.h"
#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/ColorModel.h"
#include "edit/ColorSetEdits.h"
#include "panels/ColorSetPanel.h"
#include "panels/EditColorBar.h"
#include "render/ProjectGeometry.h"

using colorizing::app::CanvasEditContext;
using colorizing::app::CanvasEditController;
using colorizing::app::CanvasEditLayer;
using colorizing::app::CanvasEditOutputs;
using colorizing::app::ColorPickerDialog;
using colorizing::app::ColorSetPanel;
using colorizing::app::EditColorBar;
using colorizing::app::NativeKeyCodes;
using colorizing::document::ProjectDocument;
using colorizing::edit::DrawColorMode;
using colorizing::edit::DrawingTool;
using colorizing::render::CanvasHit;
using colorizing::render::CanvasRegion;
using colorizing::render::PlaneLayout;
using colorizing::render::ProjectGeometry;
using colorizing::render::Resolution;
using colorizing::render::Size;
namespace document = colorizing::document;
namespace edit = colorizing::edit;
namespace app = colorizing::app;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: color widgets: " << message << '\n';
        std::exit(1);
    }
}

template <typename Widget>
Widget* child(QWidget& parent, const char* name)
{
    auto* widget = parent.findChild<Widget*>(QString::fromLatin1(name));
    expect(widget != nullptr, std::string("missing child ") + name);
    return widget;
}

uint8_t shadeOf(int frameIndex, int x, int y, uint32_t colorCount)
{
    return static_cast<uint8_t>((x + 2 * y + frameIndex) % static_cast<int>(colorCount));
}

// Three frames whose originals differ; colorized pixels are 0 except a 0x0f0f block in
// frame 0 at x 10..14, y 3..5, which the flood fill tests use.
ProjectDocument makeDocument(PlaneLayout layout, uint32_t colorCount)
{
    const ProjectGeometry geometry(layout);
    document::ProjectSnapshot snapshot;
    snapshot.romName = "colors";
    snapshot.colorCount = colorCount;
    snapshot.geometry = geometry;
    const Size rom = geometry.romFrameSize();
    for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
        document::ProjectFrameSnapshot frame;
        frame.originalSd = document::makeIndexedFrame(rom, 0);
        for (int y = 0; y < rom.height; ++y) {
            for (int x = 0; x < rom.width; ++x) {
                frame.originalSd.pixels[static_cast<std::size_t>(y * rom.width + x)] =
                    shadeOf(frameIndex, x, y, colorCount);
            }
        }
        frame.colorizedSd = document::makeRgb565Frame(geometry.planeSize(Resolution::Sd), 0);
        if (layout != PlaneLayout::NativeOriginal) {
            frame.colorizedHd = document::makeRgb565Frame(geometry.planeSize(Resolution::Hd), 0);
        }
        frame.comparisonMaskId = 255;
        snapshot.frames.push_back(frame);
    }
    const Resolution native = geometry.nativePlane();
    document::Rgb565Frame& block =
        native == Resolution::Sd ? snapshot.frames[0].colorizedSd : *snapshot.frames[0].colorizedHd;
    for (int y = 3; y <= 5; ++y) {
        for (int x = 10; x <= 14; ++x) {
            block.pixels[static_cast<std::size_t>(y * block.size.width + x)] = 0x0f0f;
        }
    }
    snapshot.backgrounds.push_back(document::BackgroundRecord{
        document::makeRgb565Frame(geometry.planeSize(Resolution::Sd), 0)});
    for (std::size_t slot = 0; slot < document::kEditColorCount; ++slot) {
        snapshot.editorMetadata.editColors[slot] = static_cast<uint16_t>(0x2000 + slot);
    }
    ProjectDocument document = ProjectDocument::fromSnapshot(snapshot);
    document.markClean();
    return document;
}

uint16_t replaced(const ProjectDocument& document, int frameIndex, int romX, int romY)
{
    return static_cast<uint16_t>(0x2000 +
                                 shadeOf(frameIndex, romX, romY, document.colorCount()));
}

struct Owner {
    explicit Owner(ProjectDocument& editedDocument)
        : document(editedDocument)
    {
    }

    CanvasEditOutputs outputs()
    {
        CanvasEditOutputs out;
        out.commit = [this](std::unique_ptr<edit::EditCommand> command) {
            command->apply(document);
            commits.push_back(command->target());
        };
        out.showStatus = [this](const QString& text, int) { status = text.toStdString(); };
        out.pickColor = [this](uint16_t color) { picked.push_back(color); };
        out.pickEditColorSlot = [this](uint16_t color) { slotPicks.push_back(color); };
        return out;
    }

    ProjectDocument& document;
    std::vector<edit::EditTarget> commits;
    std::string status;
    std::vector<uint16_t> picked;
    std::vector<uint16_t> slotPicks;
};

CanvasEditContext replaceContext(const ProjectDocument& document,
                                 DrawingTool tool,
                                 std::vector<std::size_t> frames)
{
    CanvasEditContext context;
    context.document = &document;
    context.currentFrame = frames.front();
    context.selectedFrames = std::move(frames);
    context.tool = tool;
    context.drawColor = 0xffff;
    context.drawColorMode = DrawColorMode::ColorSetReplace;
    context.selectedPalette = 0;
    return context;
}

CanvasHit at(int x, int y, Resolution plane)
{
    return CanvasHit{CanvasRegion::ColorizedFrame, plane, x, y};
}

void testSetReplaceDrawing(PlaneLayout layout, uint32_t colorCount)
{
    const std::string label = "set replace layout " + std::to_string(static_cast<int>(layout)) +
                              ", " + std::to_string(colorCount) + " colors: ";
    ProjectDocument document = makeDocument(layout, colorCount);
    const ProjectGeometry geometry = document.geometry();
    Owner owner(document);
    CanvasEditController controller(owner.outputs());

    for (Resolution plane : {Resolution::Sd, Resolution::Hd}) {
        if (!geometry.isEditable(plane)) {
            continue;
        }
        const int scale = geometry.planeScale(plane);
        CanvasEditContext context = replaceContext(document, DrawingTool::Point, {0, 1, 2});
        controller.colorizedPixelClicked(context, at(7, 9, plane), false);
        for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
            expect(document.colorizedPixel(static_cast<std::size_t>(frameIndex), plane, 7, 9) ==
                       replaced(document, frameIndex, 7 / scale, 9 / scale),
                   label + "a point takes each frame's own shade");
        }

        context.tool = DrawingTool::FilledRectangle;
        controller.strokeFinished(context, at(20, 10, plane), at(27, 13, plane), false);
        for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
            for (int y = 10; y <= 13; ++y) {
                for (int x = 20; x <= 27; ++x) {
                    expect(document.colorizedPixel(static_cast<std::size_t>(frameIndex), plane,
                                                   x, y) ==
                               replaced(document, frameIndex, x / scale, y / scale),
                           label + "a shape takes each pixel's shade");
                }
            }
        }
        expect(document.colorizedPixel(0, plane, 28, 10) == 0, label + "outside the shape");

        context.tool = DrawingTool::Line;
        context.selectedFrames = {1};
        context.currentFrame = 1;
        controller.strokeFinished(context, at(0, 20, plane), at(5, 20, plane), false);
        expect(document.colorizedPixel(1, plane, 3, 20) ==
                       replaced(document, 1, 3 / scale, 20 / scale) &&
                   document.colorizedPixel(0, plane, 3, 20) == 0,
               label + "only the selected frames");
    }

    // Flood fill: the region is the connected pixels of the current frame's color; each
    // selected frame gets its own shades there.
    const Resolution native = geometry.nativePlane();
    owner.commits.clear();
    CanvasEditContext fill = replaceContext(document, DrawingTool::FloodFill, {0, 2});
    controller.colorizedPixelClicked(fill, at(12, 4, native), false);
    expect(owner.commits.size() == 2, label + "the fill edits both frames");
    for (int frameIndex : {0, 2}) {
        for (int y = 3; y <= 5; ++y) {
            for (int x = 10; x <= 14; ++x) {
                expect(document.colorizedPixel(static_cast<std::size_t>(frameIndex), native, x,
                                               y) == replaced(document, frameIndex, x, y),
                       label + "fill region in set replace");
            }
        }
        expect(document.colorizedPixel(static_cast<std::size_t>(frameIndex), native, 9, 4) == 0 &&
                   document.colorizedPixel(static_cast<std::size_t>(frameIndex), native, 12, 6) ==
                       0,
               label + "the fill stops at the region");
    }

    // Single color mode is unchanged.
    CanvasEditContext single = replaceContext(document, DrawingTool::Point, {0});
    single.drawColorMode = DrawColorMode::SingleColor;
    single.drawColor = 0x1234;
    controller.colorizedPixelClicked(single, at(40, 30, native), false);
    expect(document.colorizedPixel(0, native, 40, 30) == 0x1234, label + "single color");
}

void testSetReplaceOnBackground()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary, 4);
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context = replaceContext(document, DrawingTool::Point, {2});
    context.layer = CanvasEditLayer::BackgroundImage;
    context.paintTarget = colorizing::app::PaintTarget::Background;
    context.selectedBackground = 0;
    controller.colorizedPixelClicked(context, at(5, 6, Resolution::Sd), false);
    expect(document.backgroundPixel(0, Resolution::Sd, 5, 6) == replaced(document, 2, 5, 6),
           "a background takes the shades of the current frame");
}

void testPipetteWithShift()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary, 4);
    Owner owner(document);
    CanvasEditController controller(owner.outputs());
    CanvasEditContext context = replaceContext(document, DrawingTool::ColorPicker, {0});
    controller.colorizedPixelClicked(context, at(11, 4, Resolution::Sd), false);
    expect(owner.picked == std::vector<uint16_t>{0x0f0f} && owner.slotPicks.empty(),
           "the pipette sets the draw color");
    controller.colorizedPixelClicked(context, at(11, 4, Resolution::Sd), true);
    expect(owner.picked.size() == 1 && owner.slotPicks == std::vector<uint16_t>{0x0f0f},
           "with Shift it asks for the edit color slot of the color");
}

void testColorPickerFields()
{
    ColorPickerDialog dialog(0xf800, QStringLiteral("Test"));
    expect(dialog.objectName() == QStringLiteral("colorPickerDialog") &&
               dialog.windowTitle() == QStringLiteral("Test"),
           "picker names");
    auto* hex = child<QLineEdit>(dialog, "colorPickerHex");
    auto* red8 = child<QSpinBox>(dialog, "colorPickerRed8");
    auto* green8 = child<QSpinBox>(dialog, "colorPickerGreen8");
    auto* blue8 = child<QSpinBox>(dialog, "colorPickerBlue8");
    auto* rgb565 = child<QSpinBox>(dialog, "colorPickerRgb565");
    auto* red5 = child<QSpinBox>(dialog, "colorPickerRed5");
    auto* green6 = child<QSpinBox>(dialog, "colorPickerGreen6");
    auto* blue5 = child<QSpinBox>(dialog, "colorPickerBlue5");
    auto* hue = child<QDoubleSpinBox>(dialog, "colorPickerHue");
    auto* saturation = child<QDoubleSpinBox>(dialog, "colorPickerSaturation");
    auto* value = child<QDoubleSpinBox>(dialog, "colorPickerValue");
    expect(hex->text() == QStringLiteral("FF0000") && red8->value() == 255 &&
               rgb565->value() == 0xf800 && red5->value() == 31 && green6->value() == 0 &&
               hue->value() == 0.0 && saturation->value() == 100.0 && value->value() == 100.0,
           "initial fields");

    green8->setValue(130);
    expect(dialog.rgb565() == ((31 << 11) | ((130 >> 2) << 5)) &&
               hex->text() == QStringLiteral("FF8200") && green6->value() == 32 &&
               rgb565->value() == dialog.rgb565(),
           "G8 updates the others");
    blue5->setValue(31);
    expect(blue8->value() == 255 && green8->value() == 130 && red8->value() == 255,
           "B5 changes blue only");
    hue->setValue(240.0);
    expect(dialog.rgb888().blue == 255 && dialog.rgb888().red < 255 && red5->value() < 31,
           "H updates the RGB fields");
    hex->setText(QStringLiteral("123456"));
    Q_EMIT hex->textEdited(hex->text());
    expect(red8->value() == 0x12 && green8->value() == 0x34 && blue8->value() == 0x56 &&
               dialog.rgb565() == edit::rgb888ToRgb565(edit::Rgb888{0x12, 0x34, 0x56}),
           "hex updates the others");
    hex->setText(QStringLiteral("12"));
    Q_EMIT hex->textEdited(hex->text());
    Q_EMIT hex->editingFinished();
    expect(hex->text() == QStringLiteral("123456"), "a half typed hex value is dropped");

    // RGB565 colors set through the RGB565 field read back through every other field unchanged
    // when that field's shown value is entered again. Every fifth color, to keep the test
    // short; the conversions themselves are checked for all 65536 in the core tests.
    for (uint32_t color = 0; color <= 0xffff; color += 5) {
        rgb565->setValue(static_cast<int>(color));
        expect(dialog.rgb565() == color, "RGB565 field");
        const QString shownHex = hex->text();
        const int r8 = red8->value();
        const int g8 = green8->value();
        const int b8 = blue8->value();
        const double h = hue->value();
        const double s = saturation->value();
        const double v = value->value();
        expect(red5->value() == static_cast<int>(color >> 11) &&
                   green6->value() == static_cast<int>((color >> 5) & 0x3f) &&
                   blue5->value() == static_cast<int>(color & 0x1f),
               "R5/G6/B5 fields");
        dialog.setRgb565(static_cast<uint16_t>(color ^ 0xffff));
        hex->setText(shownHex);
        Q_EMIT hex->textEdited(shownHex);
        expect(dialog.rgb565() == color, "hex re-entered");
        dialog.setRgb565(static_cast<uint16_t>(color ^ 0xffff));
        red8->setValue(r8);
        green8->setValue(g8);
        blue8->setValue(b8);
        expect(dialog.rgb565() == color, "R8/G8/B8 re-entered");
        dialog.setRgb565(static_cast<uint16_t>(color ^ 0xffff));
        hue->setValue(h);
        saturation->setValue(s);
        value->setValue(v);
        expect(dialog.rgb565() == color,
               "H/S/V re-entered: " + std::to_string(color) + " " + std::to_string(h) + "/" +
                   std::to_string(s) + "/" + std::to_string(v));
    }
}

void testRecentColors()
{
    QTemporaryDir directory;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("ColorWidgetTests"));
    QSettings().remove(QStringLiteral("colorPicker/recentColors"));
    expect(ColorPickerDialog::recentColors().empty(), "no recent colors yet");
    for (uint16_t color = 1; color <= 9; ++color) {
        ColorPickerDialog::rememberRecentColor(color);
    }
    ColorPickerDialog::rememberRecentColor(5);
    expect(ColorPickerDialog::recentColors() ==
               std::vector<uint16_t>{5, 9, 8, 7, 6, 4, 3, 2},
           "most recent first, at most 8, no duplicates");
    ColorPickerDialog dialog(0, QStringLiteral("Recent"));
    auto* first = child<QToolButton>(dialog, "colorPickerRecent0");
    first->click();
    expect(dialog.rgb565() == 5, "a recent color is picked by a click");
    QSettings().remove(QStringLiteral("colorPicker/recentColors"));
    QCoreApplication::setOrganizationName(QString());
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}

// The project's palettes in the picker: one shown at first, a click takes its color.
void testColorPickerPalettes()
{
    std::vector<app::ColorPickerPalette> palettes(2);
    palettes[0].name = QStringLiteral("Gr\u00fcn");
    palettes[0].colors.assign(64, 0x07e0);
    palettes[1].name = QStringLiteral("Blau");
    palettes[1].colors.assign(64, 0x001f);
    palettes[1].colors[5] = 0x1234;
    ColorPickerDialog dialog(0xf800, QStringLiteral("Palettes"), nullptr, palettes, 1);
    auto* comboBox = dialog.findChild<QComboBox*>();
    expect(comboBox && comboBox->count() == 2 && comboBox->currentIndex() == 1 &&
               comboBox->currentText() == QStringLiteral("1 - Blau"),
           "the picker shows the palette it was given first");
    QList<QToolButton*> swatches;
    for (QWidget* grid : dialog.findChildren<QWidget*>()) {
        if (grid->toolTip() == QStringLiteral("Click a color of the palette to take it")) {
            swatches = grid->findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
        }
    }
    expect(swatches.size() == 64, "with its 64 colors");
    swatches[5]->click();
    expect(dialog.rgb565() == 0x1234, "a click takes the palette's color");
    comboBox->setCurrentIndex(0);
    swatches[5]->click();
    expect(dialog.rgb565() == 0x07e0, "another palette, its colors");
    ColorPickerDialog plain(0xf800, QStringLiteral("Plain"));
    expect(!plain.findChild<QComboBox*>(), "without palettes, no palette row");
}

void testEditColorBar()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary, 4);
    EditColorBar bar;
    std::vector<std::size_t> clicks;
    std::vector<std::pair<std::size_t, uint16_t>> drops;
    std::vector<std::size_t> edits;
    std::vector<DrawColorMode> modes;
    bar.onSlotClicked = [&](std::size_t slot) { clicks.push_back(slot); };
    bar.onColorDropped = [&](std::size_t slot, uint16_t color) { drops.push_back({slot, color}); };
    bar.onSlotEditRequested = [&](std::size_t slot) { edits.push_back(slot); };
    bar.onDrawColorModeChanged = [&](DrawColorMode mode) { modes.push_back(mode); };
    bar.setSlotKeyNames(app::editColorSlotKeyNames());
    bar.refresh(&document);
    std::vector<QToolButton*> swatches;
    for (int slot = 0; slot < 16; ++slot) {
        const QByteArray name = QStringLiteral("editColorSwatch%1").arg(slot).toLatin1();
        swatches.push_back(child<QToolButton>(bar, name.constData()));
    }
    for (int slot = 0; slot < 16; ++slot) {
        expect(swatches[static_cast<std::size_t>(slot)]->isVisibleTo(&bar) == (slot < 4),
               "4 swatches in a 4-color project");
    }
    expect(swatches[2]->toolTip() ==
               QStringLiteral("Edit color 2: 0x2002, replaces shade 2 (key 3)"),
           "swatch tool tip");
    swatches[2]->click();
    expect(clicks == std::vector<std::size_t>{2} && !swatches[2]->isChecked(),
           "a click is reported; the owner selects");
    bar.setSelectedSlot(2);
    expect(swatches[2]->isChecked() && !swatches[1]->isChecked(), "selected slot");

    QMimeData mime;
    mime.setData(QStringLiteral("application/x-colorizing-rgb565"), QByteArray::number(0x07e0));
    QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    // Straight to the widget: QApplication routes drop events only within a drag.
    static_cast<QObject*>(swatches[1])->event(&drop);
    expect(drops.size() == 1 && drops[0].first == 1 && drops[0].second == 0x07e0,
           "a dropped palette color");

    bar.setDrawColorMode(DrawColorMode::ColorSetReplace);
    expect(bar.drawColorMode() == DrawColorMode::ColorSetReplace, "mode shown");
    for (QToolButton* button : bar.findChildren<QToolButton*>()) {
        if (button->text() == QStringLiteral("1 Color")) {
            button->click();
        }
    }
    expect(modes == std::vector<DrawColorMode>{DrawColorMode::SingleColor}, "mode reported");

    ProjectDocument sixteen = makeDocument(PlaneLayout::Ordinary, 16);
    bar.refresh(&sixteen);
    expect(swatches[15]->isVisibleTo(&bar), "16 swatches in a 16-color project");
    bar.refresh(nullptr);
    expect(!swatches[0]->isVisibleTo(&bar), "no swatches without a project");
}

void testColorSetPanel()
{
    ProjectDocument document = makeDocument(PlaneLayout::Ordinary, 4);
    document::ColorSetTables tables = document.colorSetTables();
    tables = edit::withStoredColorSet(tables, 3, 4);
    tables = edit::withColorSetName(tables, 3, "Gold");
    document.setColorSetTables(tables);
    ColorSetPanel panel;
    std::vector<std::size_t> stores;
    std::vector<std::size_t> loads;
    std::vector<std::size_t> deactivations;
    std::vector<std::pair<std::size_t, QString>> names;
    panel.onStoreRequested = [&](std::size_t set) { stores.push_back(set); };
    panel.onLoadRequested = [&](std::size_t set) { loads.push_back(set); };
    panel.onDeactivateRequested = [&](std::size_t set) { deactivations.push_back(set); };
    panel.onNameEdited = [&](std::size_t set, const QString& name) {
        names.push_back({set, name});
    };
    panel.refresh(&document, true);
    auto* list = panel.findChild<QListWidget*>();
    expect(list && list->count() == 64 && list->item(3)->text() == QStringLiteral("03  Gold") &&
               list->item(4)->text() == QStringLiteral("04  (inactive)"),
           "set rows");
    expect(panel.selectedSet() == std::optional<std::size_t>(0), "the current color set");
    QToolButton* store = nullptr;
    QToolButton* load = nullptr;
    QToolButton* deactivate = nullptr;
    for (QToolButton* button : panel.findChildren<QToolButton*>()) {
        if (button->text() == QStringLiteral("Store")) store = button;
        if (button->text() == QStringLiteral("Load")) load = button;
        if (button->text() == QStringLiteral("Deactivate")) deactivate = button;
    }
    auto* name = panel.findChild<QLineEdit*>();
    expect(store && load && deactivate && name, "panel controls");
    expect(store->isEnabled() && !load->isEnabled() && !deactivate->isEnabled() &&
               !name->isEnabled(),
           "an inactive set can only be stored");
    panel.selectSet(3);
    expect(load->isEnabled() && deactivate->isEnabled() && name->isEnabled() &&
               name->text() == QStringLiteral("Gold"),
           "an active set");
    store->click();
    load->click();
    deactivate->click();
    name->setText(QStringLiteral("Amber"));
    Q_EMIT name->editingFinished();
    name->setText(QStringLiteral("Amber"));
    expect(stores == std::vector<std::size_t>{3} && loads == std::vector<std::size_t>{3} &&
               deactivations == std::vector<std::size_t>{3} && names.size() == 1 &&
               names[0].second == QStringLiteral("Amber"),
           "requests");
    Q_EMIT list->itemDoubleClicked(list->item(3));
    expect(loads.size() == 2, "a double click loads");
    document.setColorSetTables(edit::withColorSetName(document.colorSetTables(), 3, "Amber"));
    panel.refresh(&document, false);
    expect(panel.selectedSet() == std::optional<std::size_t>(3) &&
               list->item(3)->text() == QStringLiteral("03  Amber"),
           "a refresh keeps the selection");

    std::vector<std::size_t> scrolls;
    panel.onScrolled = [&](std::size_t first) { scrolls.push_back(first); };
    panel.resize(250, 300);
    panel.show();
    QApplication::processEvents();
    list->scrollToItem(list->item(30), QAbstractItemView::PositionAtTop);
    panel.selectSet(50);
    expect(scrolls.empty() && list->verticalScrollBar()->value() > 0,
           "scrolling by a program is not reported");
    list->verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMaximum);
    expect(scrolls.size() == 1 && scrolls[0] == panel.firstVisibleSet() && scrolls[0] <= 56 &&
               list->verticalScrollBar()->value() >= static_cast<int>(scrolls[0]),
           "scrolling by the user is reported, at most the Win32 dialog's 56");
    document.setFirstDisplayedColorSet(20);
    panel.refresh(&document, true);
    expect(scrolls.size() == 1 && panel.firstVisibleSet() == 20,
           "a loaded project shows its first displayed set at the top");
    panel.hide();
}

void testEditColorKeys()
{
    const auto slot = [](NativeKeyCodes codes, uint32_t scanCode, uint32_t virtualKey) {
        return app::editColorSlotForNativeKey(codes, scanCode, virtualKey);
    };
    expect(slot(NativeKeyCodes::WindowsScanCode, 2, 0) == std::optional<std::size_t>(0) &&
               slot(NativeKeyCodes::WindowsScanCode, 16, 0) == std::optional<std::size_t>(4) &&
               slot(NativeKeyCodes::WindowsScanCode, 33, 0) == std::optional<std::size_t>(11) &&
               slot(NativeKeyCodes::WindowsScanCode, 47, 0) == std::optional<std::size_t>(15) &&
               !slot(NativeKeyCodes::WindowsScanCode, 6, 0),
           "Windows scan codes are the Win32 editor's");
    expect(slot(NativeKeyCodes::MacVirtualKey, 0, 0x12) == std::optional<std::size_t>(0) &&
               slot(NativeKeyCodes::MacVirtualKey, 0, 0x0c) == std::optional<std::size_t>(4) &&
               slot(NativeKeyCodes::MacVirtualKey, 0, 0x00) == std::optional<std::size_t>(8) &&
               slot(NativeKeyCodes::MacVirtualKey, 0, 0x09) == std::optional<std::size_t>(15),
           "macOS virtual keys");
    expect(slot(NativeKeyCodes::XkbKeyCode, 10, 0) == std::optional<std::size_t>(0) &&
               slot(NativeKeyCodes::XkbKeyCode, 38, 0) == std::optional<std::size_t>(8) &&
               slot(NativeKeyCodes::XkbKeyCode, 55, 0) == std::optional<std::size_t>(15),
           "XKB key codes");

    const auto event = [](int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                          quint32 scanCode = 0, quint32 virtualKey = 0) {
        return QKeyEvent(QEvent::KeyPress, key, modifiers, scanCode, virtualKey, 0);
    };
    expect(app::editColorSlotForKeyEvent(event(Qt::Key_3), 4, NativeKeyCodes::None) ==
                   std::optional<std::size_t>(2) &&
               !app::editColorSlotForKeyEvent(event(Qt::Key_Q), 4, NativeKeyCodes::None) &&
               app::editColorSlotForKeyEvent(event(Qt::Key_Q), 16, NativeKeyCodes::None) ==
                   std::optional<std::size_t>(4) &&
               app::editColorSlotForKeyEvent(event(Qt::Key_V), 16, NativeKeyCodes::None) ==
                   std::optional<std::size_t>(15),
           "Qt keys as QWERTY without native codes; letters only with 16 colors");
    expect(!app::editColorSlotForKeyEvent(event(Qt::Key_1, Qt::ControlModifier), 16,
                                          NativeKeyCodes::None) &&
               !app::editColorSlotForKeyEvent(event(Qt::Key_A, Qt::ShiftModifier), 16,
                                              NativeKeyCodes::None) &&
               app::editColorSlotForKeyEvent(event(Qt::Key_1, Qt::ShiftModifier), 16,
                                             NativeKeyCodes::None) ==
                   std::optional<std::size_t>(0) &&
               !app::editColorSlotForKeyEvent(event(Qt::Key_2, Qt::KeypadModifier), 16,
                                              NativeKeyCodes::None),
           "modifiers");
    // On an AZERTY keyboard the key left of Z sends 'A' with the scan code of QWERTY's Q.
    expect(app::editColorSlotForKeyEvent(event(Qt::Key_A, Qt::NoModifier, 16, 0), 16,
                                         NativeKeyCodes::WindowsScanCode) ==
               std::optional<std::size_t>(4),
           "the place of the key counts, not its letter");
    expect(app::editColorSlotKeyNames().size() == 16 &&
               app::editColorSlotKeyNames()[12] == QStringLiteral("Z"),
           "key names");
}

void testRgb565ToColorIsTheCoreExpansion()
{
    for (uint32_t value = 0; value <= 0xffff; ++value) {
        const QColor color = app::rgb565ToColor(static_cast<uint16_t>(value));
        const edit::Rgb888 rgb = edit::rgb565ToRgb888(static_cast<uint16_t>(value));
        expect(color.red() == rgb.red && color.green() == rgb.green && color.blue() == rgb.blue,
               "the picker shows colors as the canvas does");
    }
}

}  // namespace

void runColorWidgetTests()
{
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        testSetReplaceDrawing(layout, 4);
        testSetReplaceDrawing(layout, 16);
    }
    testSetReplaceOnBackground();
    testPipetteWithShift();
    testColorPickerFields();
    testColorPickerPalettes();
    testRecentColors();
    testEditColorBar();
    testColorSetPanel();
    testEditColorKeys();
    testRgb565ToColorIsTheCoreExpansion();
}

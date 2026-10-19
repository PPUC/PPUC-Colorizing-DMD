// The dump import dialog, the same-frames panel and the overlay of differing pixels.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDeadlineTimer>
#include <QDialogButtonBox>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QToolButton>

#include "dialogs/DumpImportDialog.h"
#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "FramePreviewWidget.h"
#include "import/FrameDumpImport.h"
#include "panels/SameFramesPanel.h"
#include "platform/Utf8Path.h"

using colorizing::app::DumpImportDialog;
using colorizing::app::FramePreviewWidget;
using colorizing::app::SameFramesPanel;
using colorizing::app::SameFramesPanelState;
using colorizing::document::IndexedFrame;
using colorizing::document::ProjectDocument;
using colorizing::render::Size;

namespace document = colorizing::document;
namespace edit = colorizing::edit;
namespace importing = colorizing::importing;

namespace {

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: dump import widgets: " << message << '\n';
        std::exit(1);
    }
}

template <typename Widget>
Widget* byTip(QWidget& parent, const char* tip)
{
    Widget* found = nullptr;
    for (Widget* candidate : parent.findChildren<Widget*>()) {
        if (candidate->toolTip() == QLatin1String(tip)) {
            expect(!found, std::string("one widget with the tool tip '") + tip + "'");
            found = candidate;
        }
    }
    expect(found != nullptr, std::string("a widget with the tool tip '") + tip + "'");
    return found;
}

IndexedFrame pattern(int seed, int shades = 4)
{
    IndexedFrame frame = document::makeIndexedFrame(Size{128, 32});
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            frame.pixels[static_cast<std::size_t>(y * 128 + x)] =
                static_cast<uint8_t>((x * 7 + y * 3 + x / (seed + 2) + seed * 5) % shades);
        }
    }
    return frame;
}

std::string frameText(uint32_t timecode, const IndexedFrame& frame)
{
    static const char digits[] = "0123456789ABCDEF";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text += digits[(timecode >> shift) & 0xf];
    }
    text += "\r\n";
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            text += digits[frame.pixels[static_cast<std::size_t>(y * frame.size.width + x)]];
        }
        text += "\r\n";
    }
    return text + "\r\n";
}

QString writeFile(const QTemporaryDir& directory, const char* name, const std::string& text)
{
    const QString path = directory.filePath(QString::fromLatin1(name));
    std::ofstream stream(colorizing::platform::pathFromUtf8(path.toStdString()), std::ios::binary);
    stream << text;
    return path;
}

ProjectDocument documentOf(const std::vector<IndexedFrame>& originals)
{
    document::ProjectSnapshot snapshot;
    snapshot.romName = "widgets";
    snapshot.colorCount = 4;
    for (const IndexedFrame& original : originals) {
        snapshot.frames.push_back(document::makeUncolorizedFrameSnapshot(
            colorizing::render::ProjectGeometry(), 4, original, 30));
    }
    return ProjectDocument::fromSnapshot(std::move(snapshot));
}

void testSameFramesPanel()
{
    SameFramesPanel panel;
    auto* countLabel = byTip<QLabel>(
        panel, "Frames the runtime cannot tell from the current one under its mask and shape mode");
    auto* sameCombo = byTip<QComboBox>(panel, "Go to a frame that is the same");
    auto* deleteCurrent =
        byTip<QToolButton>(panel, "Delete the frames that are the same as the current frame");
    auto* deleteSelected =
        byTip<QToolButton>(panel, "Delete the frames that are the same as a selected frame");
    auto* deleteAll =
        byTip<QToolButton>(panel, "Delete every frame that is the same as an earlier frame");
    auto* keepSelected =
        byTip<QCheckBox>(panel, "Deleting same frames never deletes a selected frame");
    auto* updateCounts = byTip<QToolButton>(
        panel,
        "Count the same frames of the current frame for every mask, with and without shape mode");
    auto* countsList = byTip<QListWidget>(
        panel, "Same frames of the current frame per mask: full comparison / shape mode");
    auto* spotCommon = byTip<QToolButton>(
        panel, "Mark the pixels that differ among the selected frames on the original frame");
    auto* commonToMask = byTip<QToolButton>(
        panel,
        "Make the comparison mask of the current frame cover exactly the pixels that differ "
        "among the selected frames");
    auto* usingCombo = byTip<QComboBox>(panel, "List the frames that use this mask");
    auto* usingList = byTip<QListWidget>(panel, "Frames using the mask above; click to go there");
    auto* selectThem = byTip<QToolButton>(panel, "Select the frames that use the mask above");

    // Without a project nothing can be asked for.
    const std::vector<QWidget*> controls{sameCombo,    deleteCurrent, deleteSelected,
                                         deleteAll,    updateCounts,  spotCommon,
                                         commonToMask, usingCombo,    selectThem};
    for (QWidget* control : controls) {
        expect(!control->isEnabled(), "disabled without a project");
    }
    expect(countLabel->text() == QLatin1String("Same frames: -"), "no count without a frame");
    expect(!panel.keepsSelectedFrames() && !panel.isSpotCommonChecked() &&
               panel.usingMaskId() == -1,
           "the panel's own state at the start");

    int updateRequests = 0;
    int deleteCurrentRequests = 0;
    int deleteSelectedRequests = 0;
    int deleteAllRequests = 0;
    int commonToMaskRequests = 0;
    int usingMaskChanges = 0;
    int selectRequests = 0;
    std::vector<bool> spotCommonToggles;
    std::vector<std::size_t> frameRequests;
    panel.onUpdateCountsRequested = [&]() { ++updateRequests; };
    panel.onDeleteSameAsCurrentRequested = [&]() { ++deleteCurrentRequests; };
    panel.onDeleteSameAsSelectedRequested = [&]() { ++deleteSelectedRequests; };
    panel.onDeleteAllDuplicatesRequested = [&]() { ++deleteAllRequests; };
    panel.onCommonToMaskRequested = [&]() { ++commonToMaskRequests; };
    panel.onUsingMaskChanged = [&]() { ++usingMaskChanges; };
    panel.onSelectUsingMaskRequested = [&]() { ++selectRequests; };
    panel.onSpotCommonToggled = [&](bool checked) { spotCommonToggles.push_back(checked); };
    panel.onFrameRequested = [&](std::size_t frame) { frameRequests.push_back(frame); };

    // A frame with three same frames, one frame selected, no mask assigned.
    SameFramesPanelState state;
    state.hasFrame = true;
    state.sameFrames = {4, 17, 230};
    state.selectedFrameCount = 1;
    state.frameCount = 300;
    panel.refresh(state);
    expect(countLabel->text() == QLatin1String("Same frames: 3"), "the count");
    expect(sameCombo->isEnabled() && sameCombo->count() == 3 && sameCombo->currentIndex() == -1 &&
               sameCombo->itemText(1) == QLatin1String("17"),
           "the same frames are listed and none is chosen");
    expect(deleteCurrent->isEnabled() && deleteSelected->isEnabled() && deleteAll->isEnabled() &&
               updateCounts->isEnabled() && spotCommon->isEnabled(),
           "what a frame allows");
    expect(!commonToMask->isEnabled(), "Common To Mask needs a mask and two selected frames");
    state.hasAssignedMask = true;
    panel.refresh(state);
    expect(!commonToMask->isEnabled(), "one selected frame has no differing pixels");
    state.selectedFrameCount = 2;
    panel.refresh(state);
    expect(commonToMask->isEnabled(), "a mask and two selected frames");

    sameCombo->activated(2);
    expect(frameRequests == std::vector<std::size_t>({230}), "choosing a same frame asks for it");
    deleteCurrent->click();
    deleteSelected->click();
    deleteAll->click();
    updateCounts->click();
    commonToMask->click();
    expect(deleteCurrentRequests == 1 && deleteSelectedRequests == 1 && deleteAllRequests == 1 &&
               updateRequests == 1 && commonToMaskRequests == 1,
           "each button reports once");
    keepSelected->click();
    expect(panel.keepsSelectedFrames(), "keep selected is the panel's state");
    spotCommon->click();
    expect(panel.isSpotCommonChecked() && spotCommonToggles == std::vector<bool>({true}),
           "Spot Common reports its state");
    panel.setSpotCommonChecked(false);
    expect(!panel.isSpotCommonChecked() && spotCommonToggles.size() == 1,
           "setting it does not report");

    // No same frame: nothing to go to or to delete for the current frame; the last frame
    // of a project cannot be deleted at all.
    state.sameFrames.clear();
    panel.refresh(state);
    expect(countLabel->text() == QLatin1String("Same frames: 0") && !sameCombo->isEnabled() &&
               !deleteCurrent->isEnabled() && deleteAll->isEnabled(),
           "no same frame");
    state.frameCount = 1;
    panel.refresh(state);
    expect(!deleteSelected->isEnabled() && !deleteAll->isEnabled(), "one frame is never deleted");

    // More same frames than the list holds, as the Win32 editor shows it.
    state.frameCount = 20000;
    state.sameFrames.resize(edit::kMaxListedSameFrames + 50);
    for (std::size_t index = 0; index < state.sameFrames.size(); ++index) {
        state.sameFrames[index] = index + 1;
    }
    panel.refresh(state);
    expect(countLabel->text() == QLatin1String("Same frames: >=10000") &&
               sameCombo->count() == static_cast<int>(edit::kMaxListedSameFrames),
           "the list is capped: " + countLabel->text().toStdString());

    // The counts per mask.
    expect(countsList->count() == 0, "no counts before Update Same List");
    const std::vector<edit::SameFrameCounts> counts{{document::kNoComparisonMaskId, 1, 4},
                                                    {0, 3, 9},
                                                    {1, 0, 0}};
    panel.setMaskCounts(&counts);
    expect(countsList->count() == 3 &&
               countsList->item(0)->text() == QLatin1String("No mask: 1 / shape 4") &&
               countsList->item(1)->text() == QLatin1String("Mask 0: 3 / shape 9") &&
               countsList->item(2)->text() == QLatin1String("Mask 1: 0 / shape 0"),
           "one line per mask");
    panel.setMaskCounts(nullptr);
    expect(countsList->count() == 0, "stale counts are taken away");

    // Frames using a mask.
    panel.setMaskChoices(5, {2, 0, 7});
    expect(usingCombo->isEnabled() && usingCombo->count() == 4 &&
               usingCombo->itemText(0) == QLatin1String("no mask (5)") &&
               usingCombo->itemText(3) == QLatin1String("mask 2 (7)") && panel.usingMaskId() == -1,
           "the masks and how many frames use each");
    expect(usingMaskChanges == 0, "filling the choices does not report");
    usingCombo->setCurrentIndex(3);
    expect(panel.usingMaskId() == 2 && usingMaskChanges == 1, "choosing a mask reports");
    panel.setMaskChoices(6, {2, 0, 6});
    expect(panel.usingMaskId() == 2 && usingMaskChanges == 1 &&
               usingCombo->currentText() == QLatin1String("mask 2 (6)"),
           "the choice stays when the counts change");
    panel.setMaskChoices(6, {2});
    expect(panel.usingMaskId() == -1, "a mask that is gone falls back to no mask");
    panel.setFramesUsingMask({3, 8, 9});
    expect(usingList->count() == 3 && usingList->item(2)->text() == QLatin1String("9") &&
               selectThem->isEnabled(),
           "the frames using the mask");
    usingList->itemClicked(usingList->item(1));
    expect(frameRequests.back() == 8, "a click on a frame asks for it");
    selectThem->click();
    expect(selectRequests == 1, "Select Them reports");
    panel.setFramesUsingMask({});
    expect(usingList->count() == 0 && !selectThem->isEnabled(), "no frame uses the mask");

    // No frame again.
    panel.refresh(SameFramesPanelState{});
    expect(countLabel->text() == QLatin1String("Same frames: -") && sameCombo->count() == 0 &&
               !deleteAll->isEnabled() && !spotCommon->isEnabled(),
           "back to no frame");
}

// Lets the dialog's timer notice the end of the worker, as the event loop does.
void spinUntilRead(DumpImportDialog& dialog)
{
    const QDeadlineTimer deadline(20000);
    while (dialog.isAnalysisRunning() && !deadline.hasExpired()) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    expect(!dialog.isAnalysisRunning(), "the dialog notices the end of the reading by itself");
}

void testDumpImportDialog()
{
    // A Greek capital omega in the directory name: not in code page 1252, so a dump path that
    // goes through the ANSI code page on its way to the reader fails on Windows.
    QTemporaryDir directory(QDir::tempPath() + QStringLiteral("/dump_import_\u03a9_XXXXXX"));
    expect(directory.isValid(), "a temporary directory");
    // Five frames, one repeated, one shown for 5 ms.
    const std::string good = frameText(1000, pattern(1)) + frameText(1040, pattern(2)) +
                             frameText(1045, pattern(3)) + frameText(1100, pattern(1)) +
                             frameText(1150, pattern(4));
    const QString goodPath = writeFile(directory, "my_rom.v2.txt", good);
    std::string broken = good;
    broken[2 * (12 + 32 * 130 + 2) + 12 + 3 * 130 + 9] = 'Z';
    const QString brokenPath = writeFile(directory, "broken.txt", broken);

    {
        DumpImportDialog dialog(DumpImportDialog::Mode::NewProject, nullptr);
        auto* okButton = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        auto* shortCheck = byTip<QCheckBox>(
            dialog,
            "A frame is shown until the next one of the dump arrives. The last frame and a frame "
            "whose time cannot be told (30 ms is assumed) are never left out");
        auto* shortSpin = byTip<QSpinBox>(dialog, "Shortest duration of a frame that is kept");
        auto* colorsSpin =
            byTip<QSpinBox>(dialog, "Largest number of shades of a frame that is kept");
        auto* romEdit = byTip<QLineEdit>(
            dialog, "ROM name of the new project, as the runtime looks the colorization up");
        auto* stopButton = byTip<QPushButton>(dialog, "Stop reading the dump");
        auto* againButton =
            byTip<QPushButton>(dialog, "Read the dump with the options as they are now");
        expect(dialog.windowTitle() == QLatin1String("New Project From Dump") &&
                   okButton->text() == QLatin1String("Create Project"),
               "titles of a new project");
        expect(!okButton->isEnabled() && !dialog.result() && !dialog.isAnalysisRunning() &&
                   dialog.acceptProblem() == QLatin1String("No dump was read.") &&
                   !againButton->isEnabled() && !stopButton->isEnabled(),
               "nothing to accept before a dump was read");
        expect(shortSpin->minimum() == 5 && shortSpin->maximum() == 3000 &&
                   colorsSpin->minimum() == 1 && colorsSpin->maximum() == 16 &&
                   !shortSpin->isEnabled() && !colorsSpin->isEnabled(),
               "the limits of the filter, as in the Win32 editor");
        expect(!dialog.options().dropShortFrames && !dialog.options().dropColorfulFrames &&
                   !dialog.options().useMasksAndShapeModes,
               "no filter to start with");

        dialog.setDumpPath(goodPath);
        expect(dialog.isAnalysisRunning() && !okButton->isEnabled() && stopButton->isEnabled(),
               "reading starts with the file");
        expect(romEdit->text() == QLatin1String("my_rom") && romEdit->maxLength() == 63,
               "the ROM name is proposed from the file name");
        spinUntilRead(dialog);
        const importing::DumpImportResult* result = dialog.result();
        expect(result && result->counts.framesRead == 5 && result->counts.duplicates == 1 &&
                   result->counts.framesKept == 4,
               "the dump was read");
        expect(okButton->isEnabled() && dialog.acceptProblem().isEmpty() &&
                   !stopButton->isEnabled() && againButton->isEnabled(),
               "a project can be created");
        expect(colorizing::app::dumpFormatText(*result) ==
                   QLatin1String("128x32 frames, 4 colors (no shade above 3), 5 frames"),
               "format text: " + colorizing::app::dumpFormatText(*result).toStdString());
        expect(colorizing::app::dumpCountsText(*result, false) ==
                   QLatin1String("5 read, 1 duplicates, 0 too short, 0 with too many colors, 4 "
                                 "for the project"),
               "counts text: " + colorizing::app::dumpCountsText(*result, false).toStdString());
        bool summaryShown = false;
        for (const QLabel* label : dialog.findChildren<QLabel*>()) {
            summaryShown = summaryShown ||
                           label->text() == colorizing::app::dumpCountsText(*result, false);
        }
        expect(summaryShown, "the summary is shown before anything is done");

        // A ROM name the files cannot have.
        romEdit->setText(QStringLiteral("my/rom"));
        expect(!okButton->isEnabled() && dialog.acceptProblem().contains(QLatin1String("/")) &&
                   dialog.result() != nullptr,
               "a bad ROM name blocks the project, not the result");
        dialog.setRomName(QStringLiteral("my_rom"));
        expect(okButton->isEnabled() && dialog.romName() == QLatin1String("my_rom"), "a good name");

        // Options make the result stale until the dump is read again.
        shortCheck->click();
        expect(shortSpin->isEnabled() && !dialog.result() && !okButton->isEnabled() &&
                   dialog.acceptProblem().contains(QLatin1String("read the dump again")),
               "a changed option needs another reading");
        shortSpin->setValue(10);
        againButton->click();
        dialog.waitForAnalysis();
        result = dialog.result();
        expect(result && result->counts.tooShort == 1 && result->counts.framesKept == 3 &&
                   okButton->isEnabled(),
               "read again with the filter");
        expect(dialog.options().dropShortFrames && dialog.options().shortFrameLimitMs == 10,
               "the options are those shown");

        importing::DumpImportResult taken = dialog.takeResult();
        expect(taken.ok && taken.frames.size() == 3 && !dialog.result() && !okButton->isEnabled(),
               "the frames are handed over once");

        // A dump with an error says where.
        dialog.setDumpPath(brokenPath);
        dialog.waitForAnalysis();
        expect(!dialog.result() && !okButton->isEnabled() &&
                   dialog.acceptProblem().startsWith(QLatin1String("line 73: column 10: 'Z'")),
               "an error names its line: " + dialog.acceptProblem().toStdString());
        expect(romEdit->text() == QLatin1String("broken"), "the name follows the file");

        // setOptions shows the options within their limits.
        importing::DumpImportOptions options;
        options.dropColorfulFrames = true;
        options.colorLimit = 40;
        options.shortFrameLimitMs = 1;
        options.useMasksAndShapeModes = true;
        dialog.setOptions(options);
        expect(dialog.options().dropColorfulFrames && dialog.options().colorLimit == 16 &&
                   dialog.options().shortFrameLimitMs == 5 && !dialog.options().dropShortFrames &&
                   !dialog.options().useMasksAndShapeModes,
               "options are clamped, and a new project has no masks to compare with");
    }

    // Stopping, and closing while the dump is read.
    {
        std::string large;
        for (int index = 0; index < 600; ++index) {
            large += frameText(static_cast<uint32_t>(index * 20), pattern(index % 100));
        }
        const QString largePath = writeFile(directory, "large.txt", large);
        DumpImportDialog dialog(DumpImportDialog::Mode::NewProject, nullptr);
        dialog.setDumpPath(largePath);
        dialog.stopAnalysis();
        expect(!dialog.isAnalysisRunning() && !dialog.result() &&
                   dialog.acceptProblem() == QLatin1String("Reading the dump was stopped."),
               "stopped: " + dialog.acceptProblem().toStdString());
        dialog.startAnalysis();
        dialog.reject();
        expect(!dialog.isAnalysisRunning() && dialog.result() == nullptr,
               "closing the dialog ends the reading");
        DumpImportDialog abandoned(DumpImportDialog::Mode::NewProject, nullptr);
        abandoned.setDumpPath(largePath);
        // Destroyed while reading: the destructor waits for the worker.
    }

    // Adding frames to a project.
    {
        ProjectDocument project = documentOf({pattern(1), pattern(2)});
        expect(project.appendComparisonMask() == 0, "a mask");
        project.setComparisonMaskPixel(0, 0, 0, 1);
        project.setComparisonMaskId(1, 0);
        // pattern(2) with another first pixel: the project identifies it through mask 0.
        IndexedFrame masked = pattern(2);
        masked.pixels[0] = static_cast<uint8_t>(masked.pixels[0] ^ 1);
        const QString addPath =
            writeFile(directory, "more.txt",
                      frameText(100, pattern(1)) + frameText(140, masked) +
                          frameText(180, pattern(7)) + frameText(220, pattern(8)));

        DumpImportDialog dialog(DumpImportDialog::Mode::AddFrames, &project);
        auto* okButton = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        auto* masksCheck = byTip<QCheckBox>(
            dialog,
            "On: a frame is left out if the runtime would identify it as a frame of the project. "
            "Off: only if a frame of the project has exactly the same original");
        expect(dialog.windowTitle() == QLatin1String("Add Frames From Dump") &&
                   okButton->text() == QLatin1String("Add Frames") && masksCheck->isChecked() &&
                   dialog.options().useMasksAndShapeModes,
               "adding frames compares with the masks unless told otherwise");
        dialog.setDumpPath(addPath);
        dialog.waitForAnalysis();
        const importing::DumpImportResult* result = dialog.result();
        expect(result && result->counts.identicalInProject == 1 &&
                   result->counts.identifiedByMaskOrShape == 1 && result->counts.framesKept == 2,
               "two frames are in the project already");
        expect(colorizing::app::dumpCountsText(*result, true) ==
                   QLatin1String("4 read, 0 duplicates, 0 too short, 0 with too many colors, 2 "
                                 "already in the project (1 identical, 1 identified through a "
                                 "mask or shape mode), 2 to add"),
               "counts text: " + colorizing::app::dumpCountsText(*result, true).toStdString());
        expect(okButton->isEnabled(), "frames can be added");

        masksCheck->click();
        expect(!dialog.result(), "the mask option makes the result stale");
        dialog.startAnalysis();
        dialog.waitForAnalysis();
        expect(dialog.result() && dialog.result()->counts.framesKept == 3 &&
                   dialog.result()->counts.identifiedByMaskOrShape == 0,
               "without the masks only the identical frame is left out");

        // A dump the project has completely, and one that does not fit it.
        dialog.setDumpPath(writeFile(directory, "known.txt", frameText(5, pattern(2))));
        dialog.waitForAnalysis();
        expect(dialog.result() && !okButton->isEnabled() &&
                   dialog.acceptProblem() ==
                       QLatin1String("The project has every frame of this dump already."),
               "nothing to add");
        IndexedFrame big = document::makeIndexedFrame(Size{192, 64});
        dialog.setDumpPath(writeFile(directory, "big.txt", frameText(5, big)));
        dialog.waitForAnalysis();
        expect(!dialog.result() && dialog.acceptProblem().contains(QLatin1String("192x64")) &&
                   dialog.acceptProblem().contains(QLatin1String("128x32")),
               "another size is refused: " + dialog.acceptProblem().toStdString());
        dialog.setDumpPath(writeFile(directory, "shades.txt", frameText(5, pattern(3, 16))));
        dialog.waitForAnalysis();
        expect(!dialog.result() && dialog.acceptProblem().contains(QLatin1String("16 shades")),
               "too many shades are refused: " + dialog.acceptProblem().toStdString());
    }
}

QImage renderWidget(FramePreviewWidget& widget)
{
    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    widget.render(&painter);
    return image;
}

void testDifferenceOverlay()
{
    const ProjectDocument project = documentOf({pattern(1), pattern(2)});
    FramePreviewWidget widget;
    widget.resize(800, 500);
    widget.setDocument(&project);
    widget.setFrameIndex(0);
    const QImage plain = renderWidget(widget);

    IndexedFrame differing = document::makeIndexedFrame(Size{128, 32});
    widget.setDifferenceOverlay(differing);
    expect(renderWidget(widget) == plain, "an overlay without a differing pixel shows nothing");
    differing.pixels[5 * 128 + 40] = 1;
    widget.setDifferenceOverlay(differing);
    const QImage marked = renderWidget(widget);
    int changed = 0;
    QRect bounds;
    for (int y = 0; y < plain.height(); ++y) {
        for (int x = 0; x < plain.width(); ++x) {
            if (plain.pixel(x, y) != marked.pixel(x, y)) {
                ++changed;
                bounds |= QRect(x, y, 1, 1);
            }
        }
    }
    expect(changed > 0, "a differing pixel is marked");
    // One pixel of the original frame, which is drawn in one of the two halves.
    expect(bounds.width() <= 16 && bounds.height() <= 16 && bounds.width() == bounds.height(),
           "exactly one pixel of the frame is marked");
    const std::optional<colorizing::render::CanvasHit> hit = widget.hitTestAt(bounds.center());
    expect(hit && hit->region == colorizing::render::CanvasRegion::OriginalFrame &&
               hit->x == 40 && hit->y == 5,
           "it is that pixel of the original frame");

    // Another frame size than the project's is not drawn.
    widget.setDifferenceOverlay(document::makeIndexedFrame(Size{192, 64}, 1));
    expect(renderWidget(widget) == plain, "an overlay of another size is not drawn");
    widget.setDifferenceOverlay(differing);
    widget.setDifferenceOverlay(std::nullopt);
    expect(renderWidget(widget) == plain, "the overlay goes away");
}

}  // namespace

void runDumpImportWidgetTests()
{
    testSameFramesPanel();
    testDumpImportDialog();
    testDifferenceOverlay();
}

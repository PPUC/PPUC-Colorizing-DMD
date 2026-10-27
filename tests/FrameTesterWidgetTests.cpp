// The frame tester: the project loaded into libserum whole, frames of the selection or of a
// dump fed one after the other.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPushButton>
#include <QTemporaryDir>

#include "RuntimeFixtures.h"
#include "FramePictureView.h"
#include "dialogs/FrameTesterDialog.h"
#include "dialogs/ProjectTaskDialog.h"
#include "document/ProjectDocument.h"
#include "edit/SceneEdits.h"
#include "runtime/FrameTester.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: frame tester: " << message << '\n';
        std::exit(1);
    }
}

std::string dumpFrame(const document::IndexedFrame& frame, uint32_t timecode)
{
    char header[16];
    std::snprintf(header, sizeof(header), "0x%08x\n", timecode);
    std::string text = header;
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const uint8_t shade =
                frame.pixels[static_cast<std::size_t>(y) * frame.size.width + x];
            text += "0123456789abcdef"[shade & 15];
        }
        text += '\n';
    }
    return text + '\n';
}

void testCore()
{
    const document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 3));
    document::IndexedFrame unknown = document::makeIndexedFrame(document.originalFrameSize(), 0);
    std::istringstream stream(dumpFrame(document.originalFrame(0), 100) +
                              dumpFrame(unknown, 150) +
                              dumpFrame(document.originalFrame(2), 9000));
    const runtime::TesterDump dump = runtime::readTesterDump(stream, document);
    expect(dump.error.empty() && dump.frames.size() == 3, "the dump is read: " + dump.error);
    expect(dump.frames[0].durationMs == 50 && dump.frames[1].durationMs == 30 &&
               dump.frames[2].durationMs == 30,
           "a frame lasts until the next; a gap too long and the last get the default");
    expect(runtime::findProjectFrame(document, 2, dump.frames, 0, false) == 2u &&
               runtime::findProjectFrame(document, 0, dump.frames, 0, false) == 0u &&
               !runtime::findProjectFrame(document, 1, dump.frames, 0, false),
           "found after the position, going round; not found");

    std::istringstream wrongSize("0x00000000\n0123\n0123\n");
    expect(!runtime::readTesterDump(wrongSize, document).error.empty(),
           "frames of another size are refused");
}

void testDialog()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 3));
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("dump.txt"));
    {
        QFile file(path);
        expect(file.open(QIODevice::WriteOnly), "write the dump");
        document::IndexedFrame unknown =
            document::makeIndexedFrame(document.originalFrameSize(), 0);
        file.write(QByteArray::fromStdString(dumpFrame(document.originalFrame(0), 0) +
                                             dumpFrame(unknown, 40) +
                                             dumpFrame(document.originalFrame(2), 80)));
    }
    app::setProjectTaskDialogDelayMs(10000);

    int added = 0;
    {
        app::FrameTesterDialog dialog(document, {0, 2});
        dialog.onAddFrameRequested = [&added](const runtime::TesterFrame&) {
            ++added;
            return false;
        };
        expect(dialog.errorText().isEmpty(), "libserum loads the project: " +
                                                 dialog.errorText().toStdString());
        expect(!dialog.isDumpMode() && dialog.frameCount() == 2 &&
                   dialog.lastResult().identified && dialog.lastResult().frameIndex == 0u,
               "the selected frames, the first fed at once");
        expect(dialog.findProjectFrame(2, false) && dialog.position() == 1 &&
                   dialog.lastResult().frameIndex == 2u,
               "a selected frame is found");
        expect(!dialog.findProjectFrame(1, false), "one that is not selected is not");

        expect(dialog.loadDumpFile(path) && dialog.isDumpMode() && dialog.frameCount() == 3,
               "the dump is loaded");
        expect(dialog.lastResult().identified && dialog.lastResult().frameIndex == 0u,
               "its first frame is identified as frame 0");
        expect(dialog.nextUnidentified() && dialog.position() == 1 &&
                   !dialog.lastResult().identified,
               "Next Unidentified stops at the unknown frame");
        expect(dialog.infoText().contains(QStringLiteral("not identified")),
               "and says so: " + dialog.infoText().toStdString());
        expect(!dialog.nextUnidentified() && dialog.position() == 1,
               "none behind it: it stays");
        expect(dialog.findProjectFrame(2, false) && dialog.position() == 2 &&
                   dialog.lastResult().frameIndex == 2u,
               "the dump frame of project frame 2 is found and identified");
        dialog.showPosition(0);
        expect(dialog.position() == 0 && dialog.lastResult().frameIndex == 0u, "a jump back");

        dialog.setPlaying(1);
        QCoreApplication::processEvents();
        QElapsedTimer timer;
        timer.start();
        while (dialog.position() == 0 && timer.elapsed() < 3000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        expect(dialog.position() == 1, "playing steps to the next frame");
        // A frame back while playing: playback goes on from there.
        dialog.stepBy(-1);
        expect(dialog.position() == 0 && dialog.lastResult().frameIndex == 0u,
               "a frame back while playing");
        bool pausing = false;
        for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
            pausing = pausing || button->text() == QStringLiteral("Pause");
        }
        expect(pausing, "and it plays on");
        dialog.setPlaying(0);
        dialog.stepBy(1);
        dialog.stepBy(1);
        expect(dialog.position() == 2, "frame by frame forward");
        dialog.stepBy(1);
        expect(dialog.position() == 2, "the last frame without Loop is the end");
        dialog.stepBy(-1);
        expect(dialog.position() == 1 && !dialog.lastResult().identified,
               "a frame back, fed to libserum again");

        for (QPushButton* button : dialog.findChildren<QPushButton*>()) {
            if (button->text() == QStringLiteral("Add To Project")) {
                expect(button->isEnabled(), "Add To Project is offered for a dump");
                button->click();
            }
        }
        expect(added == 1, "Add To Project asks the owner");
    }
    expect(!runtime::RuntimePreview::instance().isOpen(), "closing lets go of libserum");
    app::setProjectTaskDialogDelayMs(300);
}

// Frame 0 triggers a scene of two frames: the info line names the scene libserum shows, and
// without Play scenes it shows the game's frame.
void testScenes()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 3));
    document::SceneDefinition definition;
    definition.sceneId = 50010;
    definition.frameCount = 2;
    definition.frameGroups = 1;
    definition.durationMs = 30;
    expect(static_cast<bool>(edit::addScene(document, definition)) && document.frameCount() == 5,
           "the scene's frames are added");
    document.setSceneTriggerId(0, 50010);

    app::FrameTesterDialog dialog(document, {0});
    expect(dialog.errorText().isEmpty(), "libserum loads the project with the scene: " +
                                             dialog.errorText().toStdString());
    QElapsedTimer timer;
    timer.start();
    while (dialog.sceneText().isEmpty() && timer.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    expect(dialog.sceneText().startsWith(QStringLiteral("scene 50010 plays (frame ")) &&
               dialog.infoText().contains(dialog.sceneText()) &&
               !dialog.infoText().contains(QStringLiteral("identified as")),
           "the trigger plays the scene, which is named: " + dialog.infoText().toStdString());

    QCheckBox* playScenes = nullptr;
    for (QCheckBox* box : dialog.findChildren<QCheckBox*>()) {
        if (box->text() == QStringLiteral("Play scenes")) {
            playScenes = box;
        }
    }
    expect(playScenes && playScenes->isChecked(), "Play scenes is on by default");
    playScenes->setChecked(false);
    expect(dialog.errorText().isEmpty() && dialog.position() == 0 &&
               dialog.lastResult().frameIndex == 0u,
           "without scenes the project is loaded again and the frame fed anew");
    timer.restart();
    while (timer.elapsed() < 300) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    expect(dialog.sceneText().isEmpty() && dialog.lastResult().frameIndex == 0u,
           "and the trigger plays no scene: " + dialog.infoText().toStdString());
    dialog.setPlayScenes(true);
    expect(playScenes->isChecked(), "switched on again from the dialog");
}

// A background scene that two frames trigger: libserum keeps the picture for the second
// frame and puts it on top of the scene at the next step, which the tester shows.
void testBackgroundScene()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 3));
    document::SceneDefinition definition;
    definition.sceneId = 50020;
    definition.frameCount = 100;
    definition.frameGroups = 1;
    definition.durationMs = 30;
    definition.interruptable = true;
    definition.options = document::kSceneAsBackground;
    expect(static_cast<bool>(edit::addScene(document, definition)), "the scene is added");
    document.setSceneTriggerId(0, 50020);
    document.setSceneTriggerId(1, 50020);

    {
        app::FrameTesterDialog dialog(document, {0, 1});
        expect(dialog.errorText().isEmpty(),
               "libserum loads the project with the background scene");
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 200) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        expect(dialog.sceneText().startsWith(QStringLiteral("background scene 50020 plays")),
               "the background scene plays: " + dialog.infoText().toStdString());
        dialog.showPosition(1);
        expect(dialog.lastResult().unchanged,
               "libserum keeps the picture for a frame of the same trigger: " +
                   dialog.infoText().toStdString());
        timer.restart();
        while (dialog.lastResult().unchanged && timer.elapsed() < 2000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        expect(!dialog.lastResult().unchanged && dialog.position() == 1,
               "the next step of the scene shows a new picture: " +
                   dialog.infoText().toStdString());
    }
    // Opened again, the tester loads the project anew: the scene the last opening left
    // running is gone, so the first frame shows its picture at once (libserum took it for the
    // scene's continuation and kept a picture there was none of).
    app::FrameTesterDialog again(document, {0});
    expect(!again.lastResult().unchanged && again.currentPicture(false) != nullptr,
           "opened again, the first frame shows a picture: " + again.infoText().toStdString());
}

}  // namespace

// The tester's pictures grow with the window, in whole screen pixels per frame pixel.
void testPicturesGrow()
{
    app::FramePictureView view;
    expect(view.pixelScale() == 0, "no frame, no scale");
    int rendered = 0;
    view.setFrame(render::Size{256, 64}, [&rendered](int scale) {
        ++rendered;
        QImage image(256 * scale, 64 * scale, QImage::Format_RGB32);
        image.fill(Qt::red);
        return image;
    });
    view.resize(512, 200);
    expect(view.pixelScale() == 2, "256x64 in 512 pixels: 2 per frame pixel");
    view.resize(1400, 500);
    expect(view.pixelScale() == 5, "a larger window, a larger picture");
    view.resize(1400, 130);
    expect(view.pixelScale() == 2, "the height limits it too");
    view.resize(100, 30);
    expect(view.pixelScale() == 1, "at least 1");
    view.resize(1400, 500);
    const QImage shown = view.grab().toImage();
    expect(rendered == 1 && shown.pixelColor(700, 250) == QColor(Qt::red) &&
               shown.pixelColor(5, 5) == QColor(Qt::black),
           "drawn once at that scale, centred on black");
    // A 128x32 and a 256x64 picture on one 256x64 grid show at the same size: 900 pixels
    // fit 3 per 256x64 pixel, so 6 per 128x32 pixel (not the 7 it would get alone).
    const auto drawn = [](int scale) { return QImage(scale, scale, QImage::Format_RGB32); };
    app::FramePictureView sd;
    app::FramePictureView hd;
    sd.setFrame(render::Size{128, 32}, drawn);
    hd.setFrame(render::Size{256, 64}, drawn);
    for (app::FramePictureView* picture : {&sd, &hd}) {
        picture->setLayoutSize(render::Size{256, 64});
        picture->resize(900, 300);
    }
    expect(sd.pixelScale() == 6 && hd.pixelScale() == 3 &&
               128 * sd.pixelScale() == 256 * hd.pixelScale(),
           "on one grid both pictures are equally large");
    sd.setLayoutSize(render::Size{0, 0});
    expect(sd.pixelScale() == 7, "without it the 128x32 picture takes all the room it has");
    view.clear();
    expect(!view.hasFrame() && view.pixelScale() == 0, "cleared");
}

void runFrameTesterWidgetTests()
{
    testPicturesGrow();
    testCore();
    testDialog();
    testScenes();
    testBackgroundScene();
}

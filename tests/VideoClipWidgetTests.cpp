// The movie clip reader (VideoClip) on an animated GIF, which ffmpeg reads as a video: written
// with the editor's own GIF writer, three pictures with known colors and durations.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <QColor>
#include <QTemporaryDir>

#include "MovieImport.h"
#include "VideoClip.h"
#include "dialogs/ImageImportDialog.h"
#include "export/GifWriter.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: video clip: " << message << '\n';
        std::exit(1);
    }
}

// Whether the middle of the picture has about the color (GIF and the decoder round).
bool showsColor(const QImage& image, const QColor& color)
{
    if (image.isNull()) {
        return false;
    }
    const QColor middle = image.pixelColor(image.width() / 2, image.height() / 2);
    return std::abs(middle.red() - color.red()) < 16 &&
           std::abs(middle.green() - color.green()) < 16 &&
           std::abs(middle.blue() - color.blue()) < 16;
}

void testMovieFrameTimes()
{
    expect(app::movieFrameTimes(100, {50, 30, 20}, 0) == std::vector<double>{100, 150, 180},
           "the clip follows the frames' durations");
    expect(app::movieFrameTimes(0, {50, 30, 20}, 40) == std::vector<double>{0, 40, 80},
           "or a fixed interval");
    QImage picture(100, 50, QImage::Format_RGB32);
    expect(app::croppedMoviePicture(picture, QRect(10, 5, 30, 20)).size() == QSize(30, 20) &&
               app::croppedMoviePicture(picture, QRect()).size() == QSize(100, 50) &&
               app::croppedMoviePicture(picture, QRect(90, 40, 30, 30)).size() == QSize(10, 10),
           "the crop zone, all of the picture without one, cut at its edges");
}

}  // namespace

void runVideoClipWidgetTests()
{
    testMovieFrameTimes();
    QString why;
    if (!app::VideoClip::isAvailable(&why)) {
        std::cerr << "video clip: skipped, " << why.toStdString() << '\n';
        return;
    }
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("clip.gif"));
    {
        std::ofstream file(path.toStdString(), std::ios::binary);
        exporting::GifWriter writer(file, 64, 16);
        writer.addFrame(std::vector<uint16_t>(64 * 16, 0xf800), 10);  // red, 100 ms
        writer.addFrame(std::vector<uint16_t>(64 * 16, 0x07e0), 20);  // green, 200 ms
        writer.addFrame(std::vector<uint16_t>(64 * 16, 0x001f), 30);  // blue, 300 ms
        expect(writer.finish(), "the test clip is written");
    }

    app::VideoClip clip;
    QString error;
    expect(clip.open(path, &error), "a GIF opens: " + error.toStdString());
    expect(clip.size() == QSize(64, 16), "the clip's size");
    expect(clip.durationMs() > 550 && clip.durationMs() < 650, "the clip lasts 600 ms");
    expect(showsColor(clip.frameAt(0), Qt::red), "at 0 ms the first picture");
    expect(showsColor(clip.frameAt(150), Qt::green), "at 150 ms the second");
    expect(showsColor(clip.frameAt(450), Qt::blue), "at 450 ms the third");
    expect(showsColor(clip.frameAt(50), Qt::red), "back to 50 ms: the first again (a seek)");
    expect(showsColor(clip.frameAt(299), Qt::green), "and forward to 299 ms: the second");
    expect(clip.frameAt(5000).isNull(), "nothing past the end");

    // The import window on the clip: three selected frames of 100, 200 and 300 ms, a selection
    // of the whole 128x32 plane (the clip's proportions).
    {
        app::ImageImportSetup setup;
        setup.selectionSize = render::Size{128, 32};
        setup.firstPlaneName = QStringLiteral("128x32");
        setup.durationsMs = {100, 200, 300};
        app::ImageImportDialog dialog(clip, QStringLiteral("clip.gif"), setup);
        expect(dialog.isMovie() && dialog.zone() == QRect(0, 0, 64, 16) &&
                   dialog.batch() == app::ImageImportDialog::Batch::AsTheyPlay &&
                   dialog.frameTimes() == std::vector<double>{0, 100, 300},
               "the window starts at 0 with the whole picture, following the frames' durations");
        expect(showsColor(dialog.resultPicture(), Qt::red) &&
                   dialog.resultPicture().size() == QSize(128, 32),
               "the selection gets the clip's first picture at its size");
        dialog.setStartMs(150);
        dialog.setBatch(app::ImageImportDialog::Batch::Interval, 250);
        expect(dialog.frameTimes() == std::vector<double>{150, 400, 650} &&
                   showsColor(dialog.resultPicture(), Qt::green),
               "a later start and a fixed interval");
        dialog.setBatch(app::ImageImportDialog::Batch::SamePicture);
        expect(dialog.frameTimes() == std::vector<double>{150, 150, 150},
               "or this picture for all of them");
        dialog.setStepUnit(app::ImageImportDialog::StepUnit::Second);
        dialog.step(1);
        expect(dialog.startMs() > 500 && dialog.startMs() <= clip.durationMs() &&
                   showsColor(dialog.resultPicture(), Qt::blue),
               "a step of a second, kept inside the clip");
        dialog.setStepUnit(app::ImageImportDialog::StepUnit::Hour);
        dialog.step(-1);
        expect(dialog.startMs() == 0.0, "an hour back: the start of the clip");
        dialog.setZone(QRect(8, 4, 16, 4));
        expect(dialog.zone() == QRect(8, 4, 16, 4), "a zone");
        dialog.setZone(QRect(200, 200, 10, 10));
        expect(dialog.zone() == QRect(0, 0, 64, 16), "a zone outside the picture is the whole");
    }

    app::VideoClip broken;
    const QString text = directory.filePath(QStringLiteral("text.mp4"));
    std::ofstream(text.toStdString()) << "not a movie";
    expect(!broken.open(text, &error) && !error.isEmpty(), "a file that is no movie is refused");
}

// Frames on the clipboard (FramesClipboard.h): a package copied in one editor window and read
// in another, and a clipboard without frames.

#include <cstdlib>
#include <iostream>
#include <string>

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

#include "FramesClipboard.h"
#include "RuntimeFixtures.h"
#include "document/ProjectDocument.h"
#include "edit/PackageImport.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: frames clipboard: " << message << '\n';
        std::exit(1);
    }
}

void testRoundTrip()
{
    const document::ProjectDocument source = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeFullProject(render::PlaneLayout::Ordinary));
    const document::ProjectSnapshot package = edit::makePackage(source, {1, 4});
    std::string error;
    expect(app::copyPackageToClipboard(package, error), "the frames are copied: " + error);
    const QMimeData* data = QGuiApplication::clipboard()->mimeData();
    expect(data && data->hasFormat(app::kPackageMimeType) && !data->hasText() &&
               !data->hasImage(),
           "as a package of its own type only");
    const std::optional<document::ProjectSnapshot> read = app::readPackageFromClipboard(error);
    expect(read && read->frames.size() == 2 && read->frames[1].meta.id == source.frameMeta(4).id &&
               read->sprites.size() == package.sprites.size(),
           "and read as the package: " + error);

    QGuiApplication::clipboard()->setText(QStringLiteral("not frames"));
    expect(!app::packageOnClipboard() && !app::readPackageFromClipboard(error) &&
               error.find("no frames") != std::string::npos,
           "text on the clipboard is no frames");
    auto* broken = new QMimeData;
    broken->setData(app::kPackageMimeType, QByteArray("not a zip"));
    QGuiApplication::clipboard()->setMimeData(broken);
    expect(!app::readPackageFromClipboard(error) && !error.empty(),
           "damaged frames are refused: " + error);
    QGuiApplication::clipboard()->clear();
}

}  // namespace

void runFramesClipboardWidgetTests()
{
    testRoundTrip();
}

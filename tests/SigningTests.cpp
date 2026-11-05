#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "RuntimeFixtures.h"
#include "edit/FrameMove.h"
#include "edit/SigningNotice.h"
#include "edit/StructureCommand.h"
#include "legacy/LegacyProjectSerializer.h"
#include "platform/Utf8Path.h"
#include "project/ProjectFile.h"
#include "runtime/CromcExport.h"
#include "runtime/FrameIdentification.h"
#include "runtime/RuntimePreview.h"
#include "serum-decode.h"

namespace {

using namespace colorizing;
namespace fs = std::filesystem;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: signing: " << message << '\n';
        std::exit(1);
    }
}

// An author key for the tests (the seed 0x01 repeated) and a certificate for it that a root key
// libserum does not know signed: libserum reads it and names its author, but trusts it not.
const std::array<uint8_t, 32> kTestSeed = [] {
    std::array<uint8_t, 32> seed{};
    seed.fill(0x01);
    return seed;
}();
const std::string kTestCertificate =
    "SERUM-CERTIFICATE-1:AQCnAAAAU0NSVAECAACKiOPddAnxlf1S2y08ul1yymcJvx2UEhvzdIgBtA9vXEKd7GoAAAAA"
    "CABnaXRodWI6MQsAdGVzdC1hdXRob3KDXRSU7vFYCtmDVGr+m1xbLxbsJ5/4ngfbcQrVX1PfuQCzvET2oc6kuBflJ5v"
    "3DNe1MkRicmE55OT/5NyFkA6U0JEGaWnA4LrNQKmm5jXkZl41KMx5/cnEH6DqYBxnHgM=";

fs::path uniqueDirectory(const std::string& name)
{
    static const std::string run =
        std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    return fs::temp_directory_path() / ("colorizing_qt_v2_signing_" + run + "_" + name);
}

document::ProjectDocument makeDocument(std::size_t frames)
{
    return document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, frames));
}

std::size_t whitePixels(const document::Rgb565Frame& picture)
{
    return static_cast<std::size_t>(
        std::count(picture.pixels.begin(), picture.pixels.end(), uint16_t{0xffff}));
}

// The notice is added once, as the last frame, with the words on it; frames added later and
// frames moved behind the last frame go before it, and it does not move.
void testNoticeStaysLast()
{
    document::ProjectDocument document = makeDocument(3);
    expect(!document.signingNoticeFrame(), "a project has no notice to begin with");
    std::unique_ptr<edit::StructureCommand> added = edit::addSigningNotice(document);
    expect(added != nullptr && document.frameCount() == 4 &&
               document.signingNoticeFrame() == std::size_t{3},
           "the notice is added as the last frame");
    expect(edit::addSigningNotice(document) == nullptr, "and only once");
    const document::Rgb565Frame& picture = document.colorizedFrame(3, document::Resolution::Sd);
    expect(whitePixels(picture) > 50 && whitePixels(picture) < picture.pixelCount() / 4,
           "it shows the words");

    std::vector<document::FrameRecord> frames;
    frames.push_back(document.makeUncolorizedFrame(document.originalFrame(0), 50));
    std::unique_ptr<edit::StructureCommand> more = edit::addFrames(document, std::move(frames));
    expect(document.frameCount() == 5 && document.signingNoticeFrame() == std::size_t{4} &&
               document.frameDurationMs(3) == 50,
           "a frame added goes before the notice");
    more->revert(document);
    expect(document.frameCount() == 4 && document.signingNoticeFrame() == std::size_t{3},
           "undone, it is gone");
    more->apply(document);

    std::unique_ptr<edit::FrameMoveCommand> moved = edit::moveFrames(document, {0}, 5);
    expect(moved != nullptr && document.signingNoticeFrame() == std::size_t{4} &&
               document.frameDurationMs(3) != 50,
           "a frame moved behind the last frame goes before the notice");
    expect(edit::moveFrames(document, {4}, 0) == nullptr &&
               document.signingNoticeFrame() == std::size_t{4},
           "the notice does not move");
}

// libserum never identifies the notice, and the editor's search agrees; a subset shows it as a
// frame.
void testNoticeIsNotIdentified()
{
    runtime::RuntimePreview::instance().close();
    document::ProjectDocument document = makeDocument(3);
    (void)edit::addSigningNotice(document);
    const std::size_t notice = *document.signingNoticeFrame();
    const document::IndexedFrame original = document.originalFrame(notice);
    const runtime::FrameIdentification identification(document);
    const std::vector<std::size_t> matches = identification.matches(original);
    expect(std::find(matches.begin(), matches.end(), notice) == matches.end(),
           "the editor's search leaves the notice out");

    runtime::RuntimePreview& preview = runtime::RuntimePreview::instance();
    std::string error;
    expect(preview.openProject(document, runtime::PlaneRequest::Rows32, &error),
           "the project loads: " + error);
    const runtime::RuntimeFrame shown = preview.colorize(original);
    expect(!shown.identified || shown.frameIndex != notice, "libserum does not identify it");
    preview.close();

    const runtime::RuntimeFrame still =
        preview.renderFrame(document, notice, runtime::PlaneRequest::Rows32);
    expect(still.ok() && still.identified, "a still of the notice shows it");

    const std::vector<uint8_t> block = legacy::cromSettingsBlock(document.toSnapshot());
    expect(block.size() > 12, "the settings block has a part");
    const uint32_t contents = static_cast<uint32_t>(block[8]) | (block[9] << 8) |
                              (block[10] << 16) | (static_cast<uint32_t>(block[11]) << 24);
    expect((contents & legacy::kCromSettingsNoticeFrame) != 0, "for the notice");
}

// The project file keeps the notice and a project's choice not to sign.
void testProjectFileKeepsNotice()
{
    document::ProjectDocument document = makeDocument(2);
    (void)edit::addSigningNotice(document);
    document::ColorizationSettings settings;
    settings.signCromc = false;
    document.setColorizationSettings(settings);
    const fs::path base = uniqueDirectory("file");
    fs::create_directories(base);
    const std::string path = platform::utf8FromPath(base / "notice.cdmd");
    const legacy::LegacySaveResult saved =
        project::saveProjectFile(path, document.toSnapshot(), {"test"});
    expect(saved.ok, "saved: " + saved.error.message);
    const legacy::LegacyLoadResult loaded = project::loadProjectFile(path);
    expect(loaded.ok, "loaded: " + loaded.error.message);
    expect(loaded.snapshot.frames.size() == 3 && loaded.snapshot.frames[2].signingNotice &&
               !loaded.snapshot.frames[0].signingNotice,
           "the notice is the notice again");
    expect(loaded.snapshot.colorizationSettings && !loaded.snapshot.colorizationSettings->signCromc,
           "and the project still signs nothing");
    fs::remove_all(base);
    expect(document::ColorizationSettings{}.signCromc, "a project signs by default");
}

// A certificate is read and checked as libserum does it.
void testCertificateCheck()
{
    const runtime::SignatureCheck check =
        runtime::checkSigningCertificate(kTestCertificate, kTestSeed);
    expect(check.status == runtime::SignatureCheck::Status::Modified &&
               check.name == "test-author" && check.identity == "github:1" &&
               check.reason.find("no trusted key") != std::string::npos,
           "the test certificate names its author and is not trusted: " + check.reason);
    std::array<uint8_t, 32> other{};
    other.fill(0x02);
    expect(runtime::checkSigningCertificate(kTestCertificate, other).reason.find(
               "no trusted key") != std::string::npos,
           "the chain is checked first");
    expect(runtime::checkSigningCertificate("no certificate", kTestSeed).reason ==
               "no certificate",
           "text that is none");
}

// The export signs with the key, refuses a certificate of another key, and its test variant is
// a file libserum takes for modified: it shows the notice for the first frame.
void testSignedExport()
{
    runtime::RuntimePreview::instance().close();
    document::ProjectDocument document = makeDocument(3);
    (void)edit::addSigningNotice(document);
    const document::ProjectSnapshot snapshot = document.toSnapshot();
    const fs::path base = uniqueDirectory("export");
    fs::create_directories(base);

    const fs::path unsignedFile = base / "unsigned.cROMc";
    runtime::CromcExportResult result =
        runtime::exportCromc(snapshot, base / "work", {}, unsignedFile);
    expect(result.ok && !result.isSigned, "an export without a key: " + result.error);
    expect(runtime::checkCromcSignature(unsignedFile).status ==
               runtime::SignatureCheck::Status::Unsigned,
           "is unsigned");
    {
        // The license and terms come first, readable as they are.
        std::ifstream in(unsignedFile, std::ios::binary);
        std::string head(400, '\0');
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        expect(head.compare(0, 4, "CROM") == 0 &&
                   head.compare(10, 19, "Serum colorization\n") == 0 &&
                   head.find("ROM: " + snapshot.romName + "\n") != std::string::npos &&
                   head.find("License: ") != std::string::npos,
               "the cROMc starts with the colorization's text");
    }

    runtime::CromcSigning signing;
    signing.seed = kTestSeed;
    signing.certificate = kTestCertificate;
    const fs::path signedFile = base / "signed.cROMc";
    result = runtime::exportCromc(snapshot, base / "work", {}, signedFile, nullptr, signing);
    expect(result.ok && result.isSigned, "a signed export: " + result.error);
    const runtime::SignatureCheck check = runtime::checkCromcSignature(signedFile);
    expect(check.name == "test-author" && check.reason.find("no trusted key") != std::string::npos,
           "carries the certificate: " + check.reason);

    runtime::CromcSigning wrong = signing;
    wrong.seed->fill(0x02);
    result = runtime::exportCromc(snapshot, base / "work", {}, base / "wrong.cROMc", nullptr,
                                  wrong);
    expect(!result.ok && !fs::exists(base / "wrong.cROMc"),
           "a certificate of another key signs nothing");

    runtime::CromcSigning testing;
    testing.modifyAfterSigning = true;
    const fs::path altcolor = base / "altcolor";
    fs::create_directories(altcolor / snapshot.romName);
    const fs::path modified = altcolor / snapshot.romName / (snapshot.romName + ".cROMc");
    result = runtime::exportCromc(snapshot, base / "work", {}, modified, nullptr, testing);
    expect(result.ok && result.modified, "the test export: " + result.error);
    expect(runtime::checkCromcSignature(modified).status ==
               runtime::SignatureCheck::Status::Modified,
           "is modified");
    expect(Serum_Load(platform::utf8FromPath(altcolor).c_str(), snapshot.romName.c_str(), 0) !=
               nullptr,
           "and still loads");
    std::vector<uint8_t> first(snapshot.frames[0].originalSd.pixels.begin(),
                               snapshot.frames[0].originalSd.pixels.end());
    expect(Serum_Colorize(first.data()) != IDENTIFY_NO_FRAME, "a frame comes");
    Serum_Runtime_Metadata metadata{};
    metadata.size = sizeof(metadata);
    expect(Serum_GetRuntimeMetadata(&metadata) &&
               metadata.frameID == static_cast<uint32_t>(*document.signingNoticeFrame()),
           "and libserum shows the notice in its place");
    Serum_Dispose();

    // A signed file changed after signing is modified as well.
    expect(runtime::markCromcModified(signedFile) &&
               runtime::checkCromcSignature(signedFile).reason.find("no trusted key") !=
                   std::string::npos,
           "the chain is checked before the file");
    fs::remove_all(base);
}

}  // namespace

void runSigningTests()
{
    testNoticeStaysLast();
    testNoticeIsNotIdentified();
    testProjectFileKeepsNotice();
    testCertificateCheck();
    testSignedExport();
}

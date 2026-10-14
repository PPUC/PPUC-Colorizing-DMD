#include "legacy/LegacyProjectIo.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include "render/CanvasLayout.h"

using colorizing::document::ProjectDocument;
using colorizing::document::ProjectFrameSnapshot;
using colorizing::document::ProjectSnapshot;
using colorizing::document::UnsupportedLegacyPayload;
using colorizing::document::makeIndexedFrame;
using colorizing::document::makeRgb565Frame;
using colorizing::legacy::ILegacyProjectReader;
using colorizing::legacy::ILegacyProjectWriter;
using colorizing::legacy::LegacyIoError;
using colorizing::legacy::LegacyLoadResult;
using colorizing::legacy::LegacyProjectPaths;
using colorizing::legacy::LegacySaveResult;
using colorizing::legacy::makeLoadFailure;
using colorizing::legacy::makeLoadSuccess;
using colorizing::legacy::makeSaveFailure;
using colorizing::legacy::makeSaveSuccess;
using colorizing::render::Resolution;

namespace {

void fail(const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

ProjectSnapshot makeSyntheticSnapshot()
{
    ProjectSnapshot snapshot;
    snapshot.romName = "fixture_rom";
    snapshot.frames.push_back(ProjectFrameSnapshot{
        makeIndexedFrame(colorizing::render::kSdFrameSize, 2),
        makeRgb565Frame(colorizing::render::kSdFrameSize, 0x1111),
        makeRgb565Frame(colorizing::render::kHdFrameSize, 0x2222),
    });
    snapshot.unsupportedPayloads.push_back(UnsupportedLegacyPayload{"metadata-block", {4, 5, 6}});
    return snapshot;
}

class SyntheticReader final : public ILegacyProjectReader {
public:
    explicit SyntheticReader(ProjectSnapshot snapshot)
        : m_snapshot(std::move(snapshot))
    {
    }

    LegacyLoadResult load(const LegacyProjectPaths& paths,
                          colorizing::legacy::LegacyIoProgress* = nullptr) const override
    {
        if (paths.cromPath.empty() || paths.crpPath.empty()) {
            return makeLoadFailure("both .cROM and .cRP paths are required");
        }
        return makeLoadSuccess(m_snapshot);
    }

private:
    ProjectSnapshot m_snapshot;
};

class CapturingWriter final : public ILegacyProjectWriter {
public:
    LegacySaveResult save(const LegacyProjectPaths& paths,
                          const ProjectSnapshot& snapshot,
                          colorizing::legacy::LegacyIoProgress* = nullptr) const override
    {
        if (paths.cromPath.empty() || paths.crpPath.empty()) {
            return makeSaveFailure("both .cROM and .cRP paths are required");
        }
        lastSnapshot = snapshot;
        return makeSaveSuccess();
    }

    mutable ProjectSnapshot lastSnapshot;
};

void testReaderFeedsProjectDocument()
{
    const SyntheticReader reader(makeSyntheticSnapshot());
    const LegacyLoadResult result = reader.load(LegacyProjectPaths{
        "/tmp/game.cROM",
        "/tmp/game.cRP",
    });

    expect(result.ok, "synthetic reader should succeed for complete paths");

    const ProjectDocument document = ProjectDocument::fromSnapshot(result.snapshot);
    expect(document.romName() == "fixture_rom", "loaded ROM name");
    expect(document.frameCount() == 1, "loaded frame count");
    expect(document.hasHdFrame(0), "loaded HD frame presence");
    expect(document.colorizedFrame(0, Resolution::Sd).pixels.front() == 0x1111,
           "loaded SD colorized data");
    expect(document.colorizedFrame(0, Resolution::Hd).pixels.front() == 0x2222,
           "loaded HD colorized data");
    expect(document.unsupportedPayloads().size() == 1,
           "unsupported payload should survive document construction");
    expect(!document.isDirty(), "loaded document should be clean");
}

void testReaderRequiresBothPaths()
{
    const SyntheticReader reader(makeSyntheticSnapshot());
    const LegacyLoadResult result = reader.load(LegacyProjectPaths{
        "/tmp/game.cROM",
        "",
    });

    expect(!result.ok, "reader should fail without .cRP path");
    expect(result.error.message == "both .cROM and .cRP paths are required",
           "reader failure message");
}

void testWriterReceivesDocumentSnapshot()
{
    ProjectDocument document = ProjectDocument::fromSnapshot(makeSyntheticSnapshot());
    document.setColorizedPixel(0, Resolution::Sd, 0, 0, 0xabcd);

    const CapturingWriter writer;
    const LegacySaveResult result = writer.save(LegacyProjectPaths{
        "/tmp/game.cROM",
        "/tmp/game.cRP",
    }, document.toSnapshot());

    expect(result.ok, "capturing writer should succeed for complete paths");
    expect(writer.lastSnapshot.romName == "fixture_rom", "writer captured ROM name");
    expect(writer.lastSnapshot.frames.size() == 1, "writer captured frame count");
    expect(writer.lastSnapshot.frames[0].colorizedSd.pixels.front() == 0xabcd,
           "writer captured edited SD pixel");
    expect(writer.lastSnapshot.unsupportedPayloads.size() == 1,
           "writer captured unsupported payload");
}

void testWriterRequiresBothPaths()
{
    const CapturingWriter writer;
    const LegacySaveResult result = writer.save(LegacyProjectPaths{
        "",
        "/tmp/game.cRP",
    }, makeSyntheticSnapshot());

    expect(!result.ok, "writer should fail without .cROM path");
    expect(result.error.message == "both .cROM and .cRP paths are required",
           "writer failure message");
}

}  // namespace

void runLegacyProjectIoTests()
{
    testReaderFeedsProjectDocument();
    testReaderRequiresBothPaths();
    testWriterReceivesDocumentSnapshot();
    testWriterRequiresBothPaths();
}

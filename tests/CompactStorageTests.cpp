// The compact, copy-on-write storage of the document's blocks (document::CompactArray), the
// streaming load, the save from a snapshot that shares the document's blocks, progress and
// cancellation of load and save, and the memory limit of the structure undo history.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "RuntimeFixtures.h"
#include "document/CompactArray.h"
#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "edit/EditCommand.h"
#include "edit/StructureCommand.h"
#include "edit/UndoHistories.h"
#include "legacy/LegacyByteSource.h"
#include "legacy/LegacyCromFrames.h"
#include "legacy/LegacyCrpOriginals.h"
#include "legacy/LegacyProjectFileReader.h"
#include "legacy/LegacyProjectFileWriter.h"
#include "legacy/LegacyProjectSerializer.h"
#include "legacy/LegacySnapshotBuilder.h"
#include "platform/Utf8Path.h"

namespace {

using namespace colorizing;
using document::CompactArray;
using document::ProjectDocument;
using document::ProjectSnapshot;
using render::PlaneLayout;
using render::Resolution;

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

struct SerializedProject {
    std::string crom;
    std::string crp;
};

SerializedProject serialize(const ProjectSnapshot& snapshot)
{
    std::ostringstream crom;
    std::ostringstream crp;
    std::string error;
    if (!legacy::writeLegacyCrom(crom, snapshot, error) ||
        !legacy::writeLegacyCrp(crp, snapshot, error)) {
        fail("serialize failed: " + error);
    }
    return {crom.str(), crp.str()};
}

// What a load of the serialized files gives.
ProjectSnapshot parse(const SerializedProject& files)
{
    const auto* crom = reinterpret_cast<const uint8_t*>(files.crom.data());
    const auto* crp = reinterpret_cast<const uint8_t*>(files.crp.data());
    legacy::LegacyCromFrameParseResult frames =
        legacy::parseModernLegacyCromFrames(crom, files.crom.size());
    expect(frames.ok, "cROM parses: " + frames.errorMessage);
    legacy::LegacyCrpOriginalParseResult originals =
        legacy::parseLegacyCrpOriginalFrames(crp, files.crp.size(), frames.frameData.metadata);
    expect(originals.ok, "cRP parses: " + originals.errorMessage);
    legacy::LegacySnapshotBuildResult built = legacy::buildProjectSnapshot(
        std::move(frames.frameData), std::move(originals.originalData));
    expect(built.ok, "snapshot builds: " + built.errorMessage);
    return std::move(built.snapshot);
}

ProjectDocument loadedDocument(PlaneLayout layout)
{
    return ProjectDocument::fromSnapshot(
        parse(serialize(runtime_fixtures::makeFullProject(layout))));
}

// Gives every block of the snapshot a buffer of its own, as the storage before compact
// blocks had it: the oracle for "a compact or shared block is written like a dense one".
template <typename T>
void materialize(CompactArray<T>& values)
{
    if (!values.empty()) {
        (void)values.data();
    }
}

void materialize(ProjectSnapshot& snapshot)
{
    for (document::ProjectFrameSnapshot& frame : snapshot.frames) {
        materialize(frame.originalSd.pixels);
        materialize(frame.colorizedSd.pixels);
        if (frame.colorizedHd) {
            materialize(frame.colorizedHd->pixels);
        }
        if (frame.inactiveHd) {
            materialize(frame.inactiveHd->pixels);
        }
        if (frame.dynamicData) {
            materialize(frame.dynamicData->maskSd.pixels);
            materialize(frame.dynamicData->maskHd.pixels);
            materialize(frame.dynamicData->colorsSd);
            materialize(frame.dynamicData->colorsHd);
            materialize(frame.dynamicData->shadowDirectionsSd);
            materialize(frame.dynamicData->shadowColorsSd);
            materialize(frame.dynamicData->shadowDirectionsHd);
            materialize(frame.dynamicData->shadowColorsHd);
        }
        if (frame.backgroundData) {
            materialize(frame.backgroundData->maskSd.pixels);
            materialize(frame.backgroundData->maskHd.pixels);
        }
        materialize(frame.colorRotationsSd);
        materialize(frame.colorRotationsHd);
    }
    for (document::SpriteRecord& sprite : snapshot.sprites) {
        materialize(sprite.originalSd.pixels);
        materialize(sprite.colorizedSd.pixels);
        materialize(sprite.maskHd.pixels);
        materialize(sprite.colorizedHd.pixels);
        materialize(sprite.dynamicMaskSd.pixels);
        materialize(sprite.dynamicMaskHd.pixels);
        materialize(sprite.dynamicColorsSd);
        materialize(sprite.dynamicColorsHd);
    }
    for (document::BackgroundRecord& background : snapshot.backgrounds) {
        materialize(background.colorizedSd.pixels);
    }
    for (document::IndexedFrame& mask : snapshot.comparisonMasks) {
        materialize(mask.pixels);
    }
}

// ---- CompactArray

void testUniformArraysShareOneBuffer()
{
    const CompactArray<uint8_t> a(4096, 255);
    const CompactArray<uint8_t> b(4096, 255);
    const CompactArray<uint8_t> other(4096, 1);
    expect(a.isSharedUniform() && b.isSharedUniform(), "uniform arrays use a shared buffer");
    expect(a.data() == b.data(), "same size and value share one buffer");
    expect(a.data() != other.data(), "another value has a buffer of its own");
    expect(a.ownedBytes() == 0, "a shared uniform array owns no memory");
    expect(a.size() == 4096 && a[17] == 255 && a.back() == 255, "a uniform array reads its value");
    expect(a == std::vector<uint8_t>(4096, 255), "equal to the dense vector");

    CompactArray<uint16_t> adopted(std::vector<uint16_t>(256, 0x1234));
    expect(adopted.isSharedUniform(), "adopted values that are all alike become uniform");
    CompactArray<uint16_t> mixed(std::vector<uint16_t>{1, 2, 3});
    expect(!mixed.isSharedUniform() && mixed.ownedBytes() > 0, "mixed values are stored");
}

void testFirstWriteMaterialises()
{
    CompactArray<uint8_t> a(4096, 255);
    const CompactArray<uint8_t> b(4096, 255);
    const uint8_t* shared = b.data();
    a[5] = 3;
    expect(!a.isSharedUniform() && a.ownedBytes() >= 4096, "the first write materialises");
    expect(std::as_const(a)[5] == 3 && std::as_const(a)[4] == 255, "the write is kept");
    expect(b.data() == shared && b[5] == 255, "the other uniform array is unchanged");
    const CompactArray<uint8_t> fresh(4096, 255);
    expect(fresh.data() == shared,
           "the shared buffer itself is unchanged");

    a[5] = 255;
    expect(!a.isSharedUniform(), "writing the value back keeps the array materialised");
    a.compact();
    expect(a.isSharedUniform() && std::as_const(a).data() == shared,
           "compact() returns to the shared buffer");
}

void testCopiesShareUntilWritten()
{
    CompactArray<uint16_t> original(std::vector<uint16_t>{1, 2, 3, 4});
    const CompactArray<uint16_t> copy = original;
    expect(copy.data() == std::as_const(original).data(), "a copy shares the storage");
    expect(copy.sharesStorage() && copy.ownedBytes() == 0, "a shared copy owns nothing");
    original[0] = 9;
    expect(copy[0] == 1 && std::as_const(original)[0] == 9, "a write does not reach the copy");
    expect(copy.data() != std::as_const(original).data(), "the writer got its own buffer");

    // Once the copy is gone, the remaining holder writes in place.
    CompactArray<uint16_t> holder(std::vector<uint16_t>{5, 6, 7});
    {
        const CompactArray<uint16_t> temporary = holder;
        (void)temporary;
    }
    const uint16_t* before = std::as_const(holder).data();
    holder[1] = 8;
    expect(std::as_const(holder).data() == before, "the sole holder writes in place");

    CompactArray<uint8_t> grown(3, 1);
    grown.push_back(2);
    grown.resize(6, 4);
    expect(grown == std::vector<uint8_t>{1, 1, 1, 2, 4, 4}, "push_back and resize");
    grown.pop_back();
    expect(grown.size() == 5, "pop_back");
    CompactArray<uint8_t> moved = std::move(grown);
    expect(moved.size() == 5 && grown.empty(), "a moved-from array is empty");  // NOLINT
    const std::vector<uint8_t> out(moved);
    expect(out.size() == 5, "explicit conversion to a vector");
}

void testCopyIsReadWhileOriginalIsWritten()
{
    CompactArray<uint16_t> original(std::vector<uint16_t>(1 << 20, 7));
    original[0] = 1;
    const CompactArray<uint16_t> snapshot = original;
    std::atomic<uint64_t> sum{0};
    std::thread reader([&snapshot, &sum]() {
        uint64_t total = 0;
        for (int round = 0; round < 20; ++round) {
            for (uint16_t value : snapshot) {
                total += value;
            }
        }
        sum = total;
    });
    for (std::size_t i = 0; i < original.size(); i += 3) {
        original[i] = 0;
    }
    reader.join();
    const uint64_t expected = 20 * (static_cast<uint64_t>((1 << 20) - 1) * 7 + 1);
    expect(sum.load() == expected, "the copy read on another thread kept its values");
}

// ---- the document's blocks

void testLoadedDefaultBlocksAreShared()
{
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        const ProjectDocument document = loadedDocument(layout);
        std::size_t sharedMasks = 0;
        std::size_t sharedRotations = 0;
        for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
            const auto& dynamic = document.dynamicData(frame);
            expect(dynamic.has_value(), "a loaded frame has dynamic data");
            if (!document.hasDynamicPixels(frame)) {
                const auto& mask = document.dynamicPlane() == Resolution::Hd ? dynamic->maskHd
                                                                             : dynamic->maskSd;
                expect(mask.pixels.isSharedUniform(),
                       "a dynamic mask without a zone is a shared block");
                ++sharedMasks;
            }
            const Resolution native = document.geometry().nativePlane();
            if (document.colorRotationLength(frame, native, 0) == 0 &&
                document.colorRotationLength(frame, native, 1) == 0 &&
                document.colorRotationLength(frame, native, 2) == 0 &&
                document.colorRotationLength(frame, native, 3) == 0) {
                sharedRotations += document.colorRotations(frame, native).isSharedUniform() ? 1 : 0;
            }
        }
        expect(sharedMasks > 0, "some frames have no dynamic zone");
        expect(sharedRotations > 0, "rotation tables of 0 are shared");
    }
}

void testEditMaterialisesItsBlockAndUndoRestoresTheBytes()
{
    ProjectDocument document = loadedDocument(PlaneLayout::Ordinary);
    std::size_t first = document.frameCount();
    std::size_t second = document.frameCount();
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        if (!document.hasDynamicPixels(frame)) {
            (first == document.frameCount() ? first : second) = frame;
            if (second != document.frameCount()) {
                break;
            }
        }
    }
    expect(second < document.frameCount(), "two frames without a dynamic zone");
    const SerializedProject before = serialize(document.toSnapshot());
    const uint8_t* shared = document.dynamicData(second)->maskSd.pixels.data();
    expect(document.dynamicMaskPixel(first, Resolution::Sd, 3, 4) == 255, "default read");

    edit::MaskEditCommand command(edit::EditKind::DynamicMask, first, Resolution::Sd,
                                  {edit::MaskPixelChange{3, 4, 255, 2}});
    command.apply(document);
    expect(document.dynamicMaskPixel(first, Resolution::Sd, 3, 4) == 2, "the write is read");
    expect(!document.dynamicData(first)->maskSd.pixels.isSharedUniform(),
           "the edited mask is materialised");
    expect(document.dynamicData(second)->maskSd.pixels.data() == shared &&
               document.dynamicData(second)->maskSd.pixels.isSharedUniform(),
           "the other frame keeps the shared block");
    expect(document.hasDynamicPixels(first), "the frame has a dynamic zone now");
    expect(serialize(document.toSnapshot()).crom != before.crom, "the edit is saved");

    command.revert(document);
    expect(document.dynamicMaskPixel(first, Resolution::Sd, 3, 4) == 255, "undo reads 255");
    expect(!document.hasDynamicPixels(first), "no zone after the undo");
    const SerializedProject after = serialize(document.toSnapshot());
    expect(after.crom == before.crom && after.crp == before.crp,
           "after the undo the project saves to the bytes it had");
}

void testUniformBlocksKeepTheirValue()
{
    // Blocks that are uniform with values other than the editor's defaults, as files have
    // them (background masks of 0 instead of 1, dynamic masks of one set, a shared table).
    ProjectSnapshot snapshot = runtime_fixtures::makeProject(PlaneLayout::Ordinary, 4);
    const render::Size sd = snapshot.geometry.planeSize(Resolution::Sd);
    const render::Size hd = snapshot.geometry.planeSize(Resolution::Hd);
    for (document::ProjectFrameSnapshot& frame : snapshot.frames) {
        document::FrameDynamicData dynamic;
        dynamic.maskSd = document::makeIndexedFrame(sd, 7);
        dynamic.maskHd = document::makeIndexedFrame(hd, 0);
        dynamic.colorsSd.assign(32 * snapshot.colorCount, 0x1234);
        dynamic.colorsHd.assign(32 * snapshot.colorCount, 0xffff);
        dynamic.shadowDirectionsSd.assign(32, 3);
        dynamic.shadowColorsSd.assign(32, 0x00ff);
        dynamic.shadowDirectionsHd.assign(32, 0);
        dynamic.shadowColorsHd.assign(32, 0);
        frame.dynamicData = std::move(dynamic);
        document::FrameBackgroundData background;
        background.maskSd = document::makeIndexedFrame(sd, 0);
        background.maskHd = document::makeIndexedFrame(hd, 0);
        frame.backgroundData = std::move(background);
        frame.colorRotationsSd.assign(256, 0x0202);
        frame.colorRotationsHd.assign(256, 0);
    }
    const SerializedProject files = serialize(snapshot);
    ProjectSnapshot dense = snapshot;
    materialize(dense);
    const SerializedProject denseFiles = serialize(dense);
    expect(files.crom == denseFiles.crom && files.crp == denseFiles.crp,
           "uniform blocks are written exactly like dense ones");

    const ProjectDocument document = ProjectDocument::fromSnapshot(parse(files));
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        const auto& dynamic = *document.dynamicData(frame);
        const auto& background = *document.backgroundData(frame);
        expect(dynamic.maskSd.pixels.isSharedUniform() && dynamic.maskSd.pixels[100] == 7,
               "a dynamic mask of set 7 is loaded as a uniform block of 7");
        expect(background.maskSd.pixels.isSharedUniform() && background.maskSd.pixels[9] == 0,
               "a background mask of 0 stays 0");
        expect(background.maskHd.pixels.isSharedUniform() && background.maskHd.pixels[9] == 0,
               "an HD background mask of 0 stays 0");
        expect(dynamic.colorsHd.isSharedUniform() && dynamic.colorsHd[0] == 0xffff,
               "uniform dynamic colors keep their value");
        expect(document.colorRotations(frame, Resolution::Sd).isSharedUniform() &&
                   document.colorRotations(frame, Resolution::Sd)[5] == 0x0202,
               "a uniform rotation table keeps its value");
    }
    const SerializedProject again = serialize(document.toSnapshot());
    expect(again.crom == files.crom && again.crp == files.crp,
           "the loaded uniform blocks save to the same bytes");
}

void testSaveSnapshotSharesTheDocumentsBlocks()
{
    for (PlaneLayout layout :
         {PlaneLayout::Ordinary, PlaneLayout::NativeExtra, PlaneLayout::NativeOriginal}) {
        ProjectDocument document = loadedDocument(layout);
        const ProjectSnapshot snapshot = document.toSnapshot();
        expect(snapshot.frames[0].colorizedSd.pixels.data() ==
                   document.colorizedFrame(0, Resolution::Sd).pixels.data(),
               "the save snapshot shares the colorized frame");
        expect(snapshot.frames[0].originalSd.pixels.data() ==
                   document.originalFrame(0).pixels.data(),
               "the save snapshot shares the original");

        // The old way: every block copied into a snapshot of its own (the oracle).
        ProjectSnapshot dense = document.toSnapshot();
        materialize(dense);
        const SerializedProject shared = serialize(snapshot);
        const SerializedProject copied = serialize(dense);
        expect(shared.crom == copied.crom && shared.crp == copied.crp,
               "saving the shared snapshot writes the bytes of a full copy");
        expect(legacy::legacyCromByteCount(snapshot) == shared.crom.size() &&
                   legacy::legacyCrpByteCount(snapshot) == shared.crp.size(),
               "the byte counts for the progress are exact");

        // A write to the document after the snapshot was taken does not reach it.
        const uint16_t old = document.colorizedPixel(0, Resolution::Sd, 0, 0);
        document.setColorizedPixel(0, Resolution::Sd, 0, 0, static_cast<uint16_t>(old ^ 0xffff));
        expect(snapshot.frames[0].colorizedSd.pixels[0] == old,
               "the snapshot keeps the state it was taken in");
        expect(serialize(snapshot).crom == shared.crom, "and saves it");
    }
}

// ---- streaming load, progress and cancellation

std::filesystem::path uniqueTempPath(const std::string& suffix)
{
    static int counter = 0;
    static const std::string run = std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() /
           platform::pathFromUtf8("colorizing_qt_v2_compact_\xce\xa9_" + run + "_" +
                                  std::to_string(counter++) + suffix);
}

void writeFile(const std::filesystem::path& path, const std::string& bytes)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        fail("could not write " + platform::utf8FromPath(path));
    }
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

// A project of several MB, so that reads and writes pass several progress reports.
ProjectSnapshot largerProject()
{
    ProjectSnapshot snapshot = runtime_fixtures::makeProject(PlaneLayout::Ordinary, 120);
    for (std::size_t frame = 0; frame < snapshot.frames.size(); frame += 2) {
        snapshot.frames[frame].colorizedHd =
            document::makeRgb565Frame(snapshot.geometry.planeSize(Resolution::Hd),
                                      static_cast<uint16_t>(frame));
        snapshot.frames[frame].colorizedHd->pixels[1] = 1;
    }
    return snapshot;
}

void testFileReaderStreamsWithProgress()
{
    const SerializedProject files = serialize(largerProject());
    const auto cromPath = uniqueTempPath(".cROM");
    const auto crpPath = uniqueTempPath(".cRP");
    writeFile(cromPath, files.crom);
    writeFile(crpPath, files.crp);
    const legacy::LegacyProjectPaths paths{platform::utf8FromPath(cromPath),
                                           platform::utf8FromPath(crpPath)};

    legacy::LegacyIoProgress progress;
    legacy::LegacyDocumentLoadResult loaded = legacy::loadLegacyProjectDocument(paths, &progress);
    expect(loaded.ok && loaded.document, "the streamed load succeeds: " + loaded.error.message);
    expect(progress.total.load() == files.crom.size() + files.crp.size() &&
               progress.done.load() == progress.total.load(),
           "the load reports every byte");
    const SerializedProject again = serialize(loaded.document->toSnapshot());
    expect(again.crom == files.crom && again.crp == files.crp,
           "the streamed load saves to the same bytes");

    legacy::LegacyIoProgress cancelled;
    cancelled.cancel = true;
    const legacy::LegacyDocumentLoadResult none = legacy::loadLegacyProjectDocument(paths, &cancelled);
    expect(!none.ok && none.cancelled && !none.document, "a cancelled load gives no project");

    // A file that ends early is reported as before, from the stream.
    writeFile(cromPath, files.crom.substr(0, files.crom.size() / 2));
    const legacy::LegacyDocumentLoadResult truncated =
        legacy::loadLegacyProjectDocument(paths, nullptr);
    expect(!truncated.ok && !truncated.cancelled &&
               truncated.error.message.rfind("unexpected end of cROM", 0) == 0,
           "a truncated cROM fails with its block: " + truncated.error.message);

    std::filesystem::remove(cromPath);
    std::filesystem::remove(crpPath);
}

void testByteSourceAcrossBufferBoundaries()
{
    // More than one file buffer, read in pieces that straddle the refills.
    std::string bytes(3 * (1 << 20) + 17, '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<char>((i * 31 + i / 7) & 0xff);
    }
    const auto path = uniqueTempPath(".bin");
    writeFile(path, bytes);
    {
        // Closed before the file is removed: Windows does not delete an open file.
        legacy::LegacyByteSource file(platform::utf8FromPath(path));
        legacy::LegacyByteSource memory(reinterpret_cast<const uint8_t*>(bytes.data()),
                                        bytes.size());
        expect(file.isOpen() && file.size() == bytes.size(), "the file source opens");
        std::vector<uint8_t> a(4099);
        std::vector<uint8_t> b(4099);
        while (memory.remaining() >= a.size() + 6) {
            uint16_t u16a = 0;
            uint16_t u16b = 0;
            uint32_t u32a = 0;
            uint32_t u32b = 0;
            expect(file.readBytes(a.data(), a.size()) && memory.readBytes(b.data(), b.size()) &&
                       a == b,
                   "bytes agree");
            expect(file.readU16(u16a) && memory.readU16(u16b) && u16a == u16b, "u16 agree");
            expect(file.readU32(u32a) && memory.readU32(u32b) && u32a == u32b, "u32 agree");
        }
        expect(file.offset() == memory.offset(), "offsets agree");
        expect(!file.readBytes(a.data(), file.remaining() + 1), "reading past the end fails");
    }
    std::filesystem::remove(path);
}

// Sets the progress's cancel flag when the first bytes arrive, so a write is cancelled in
// the middle at a known point.
class CancellingBuffer final : public std::streambuf {
public:
    explicit CancellingBuffer(legacy::LegacyIoProgress& progress)
        : m_progress(progress)
    {
    }
    std::size_t written = 0;

protected:
    std::streamsize xsputn(const char*, std::streamsize count) override
    {
        written += static_cast<std::size_t>(count);
        m_progress.cancel = true;
        return count;
    }

private:
    legacy::LegacyIoProgress& m_progress;
};

void testWriteCancelsInTheMiddle()
{
    const ProjectSnapshot snapshot = largerProject();
    legacy::LegacyIoProgress progress;
    CancellingBuffer buffer(progress);
    std::ostream stream(&buffer);
    std::string error;
    expect(!legacy::writeLegacyCrom(stream, snapshot, error, nullptr, &progress),
           "a cancelled write fails");
    expect(error == legacy::kLegacyIoCancelledMessage, "with the cancel message");
    expect(buffer.written > 0 && buffer.written < legacy::legacyCromByteCount(snapshot),
           "it stopped after the first flush");
}

void testFileWriterCancelAndProgress()
{
    const ProjectSnapshot snapshot = largerProject();
    const auto cromPath = uniqueTempPath(".cROM");
    const auto crpPath = uniqueTempPath(".cRP");
    writeFile(cromPath, "old crom");
    writeFile(crpPath, "old crp");
    const legacy::LegacyProjectPaths paths{platform::utf8FromPath(cromPath),
                                           platform::utf8FromPath(crpPath)};
    const legacy::LegacyProjectFileWriter writer;

    legacy::LegacyIoProgress cancelled;
    cancelled.cancel = true;
    const legacy::LegacySaveResult stopped = writer.save(paths, snapshot, &cancelled);
    expect(!stopped.ok && stopped.cancelled, "a cancelled save fails as cancelled");
    expect(readFile(cromPath) == "old crom" && readFile(crpPath) == "old crp",
           "a cancelled save leaves the files as they were");
    expect(!std::filesystem::exists(platform::pathFromUtf8(paths.cromPath + ".saving")) &&
               !std::filesystem::exists(platform::pathFromUtf8(paths.crpPath + ".saving")),
           "and no temporary files");

    legacy::LegacyIoProgress progress;
    const legacy::LegacySaveResult saved = writer.save(paths, snapshot, &progress);
    expect(saved.ok && !saved.cancelled, "the save succeeds: " + saved.error.message);
    const SerializedProject files = serialize(snapshot);
    expect(readFile(cromPath) == files.crom && readFile(crpPath) == files.crp,
           "the saved files are the serialized bytes");
    expect(progress.done.load() == progress.total.load() &&
               progress.total.load() == files.crom.size() + files.crp.size(),
           "the save reports every byte");
    expect(progress.currentStage() == legacy::LegacyIoStage::Replacing, "the last stage");
    std::filesystem::remove(cromPath);
    std::filesystem::remove(crpPath);
}

// ---- undo memory

void testStructureUndoMemoryLimit()
{
    ProjectDocument document =
        ProjectDocument::fromSnapshot(runtime_fixtures::makeProject(PlaneLayout::Ordinary, 12));
    edit::UndoHistories histories;
    const edit::HistoryKey key = edit::structureHistoryKey();

    // Each deletion of one frame holds that frame's original and colorized frame.
    auto first = edit::deleteFrames(document, {0});
    const std::size_t oneFrame = first->heldBytes();
    expect(oneFrame >= 4096 + 8192, "a deleted frame is counted with its blocks");
    const SerializedProject afterFirst = serialize(document.toSnapshot());
    histories.setStructureMemoryLimit(oneFrame * 5 / 2);
    expect(histories.push(key, std::move(first)) == 0, "one step is within the limit");
    expect(histories.push(key, edit::deleteFrames(document, {0})) == 0, "two steps fit");
    expect(histories.push(key, edit::deleteFrames(document, {0})) == 1,
           "the third step drops the oldest");
    expect(histories.structureMemoryBytes() <= histories.structureMemoryLimit(),
           "the history is within the limit again");
    // The remaining steps undo to the state the dropped one left.
    histories.undo(key, document);
    histories.undo(key, document);
    expect(!histories.canUndo(key), "the dropped step cannot be undone");
    const SerializedProject restored = serialize(document.toSnapshot());
    expect(restored.crom == afterFirst.crom && restored.crp == afterFirst.crp,
           "undo reaches exactly the state after the dropped step");

    // A single step above the limit is kept: the step just made can always be undone.
    histories.redo(key, document);
    histories.setStructureMemoryLimit(1);
    expect(histories.push(key, edit::deleteFrames(document, {0, 1})) == 1,
           "everything older is dropped");
    expect(histories.canUndo(key), "the newest step stays");
    histories.undo(key, document);
    expect(!histories.canUndo(key), "nothing older is left");
}

}  // namespace

// A block of values that LZ4 packs well: runs of a few values.
std::vector<uint16_t> runs(std::size_t count, uint16_t seed)
{
    std::vector<uint16_t> values(count);
    for (std::size_t index = 0; index < count; ++index) {
        values[index] = static_cast<uint16_t>(seed + (index / 37) % 5);
    }
    return values;
}

void testPackedArrays()
{
    const std::vector<uint16_t> values = runs(4096, 100);
    CompactArray<uint16_t> array(values);
    const std::size_t unpackedBytes = array.ownedBytes();
    expect(array.packStorage() >= 4096 * 2 && array.isPacked() && array.ownedBytes() < unpackedBytes / 4,
           "a block packs to a fraction of its size");
    expect(array.packStorage() == 0, "a packed block packs no further");
    const CompactArray<uint16_t> copy(array);
    expect(copy == values && !array.isPacked() && array.ownedBytes() == 0 && copy.sharesStorage(),
           "reading a copy unpacks the buffer they share");
    expect(array.packStorage() > 0 && array.isPacked(), "packed again");

    // Read on two threads at once while packed: both get the values.
    std::atomic<int> right{0};
    std::thread first([&]() { right += copy == values ? 1 : 0; });
    std::thread second([&]() { right += array[1000] == values[1000] ? 1 : 0; });
    first.join();
    second.join();
    expect(right == 2, "a packed block is unpacked once for readers on several threads");

    array.packStorage();
    array[5] = 7;
    expect(!array.isPacked() && array[5] == 7 && array[6] == values[6] && copy[5] == values[5],
           "a write unpacks and goes to a buffer of its own");

    // Values that do not pack well are left as they are.
    std::vector<uint16_t> noise(4096);
    uint32_t state = 12345;
    for (uint16_t& value : noise) {
        state = state * 1664525u + 1013904223u;
        value = static_cast<uint16_t>(state >> 16);
    }
    CompactArray<uint16_t> random(noise);
    expect(random.packStorage() == 0 && !random.isPacked() && random == noise,
           "incompressible values stay unpacked");
    CompactArray<uint8_t> uniform(1000, 3);
    expect(uniform.packStorage() == 0 && !uniform.isPacked(), "a uniform buffer is never packed");

    CompactArray<uint16_t> same(values);
    CompactArray<uint16_t> other(runs(4096, 200));
    other.shareStorage(same);
    expect(other.storage() != same.storage(), "different values are not shared");
    CompactArray<uint16_t> twin(values);
    twin.shareStorage(same);
    expect(twin.storage() == same.storage() && twin == values, "identical values share a buffer");
    twin[0] = 9;
    expect(twin.storage() != same.storage() && same[0] == values[0], "until one is written");
}

void testDocumentSharesAndPacksBlocks()
{
    ProjectSnapshot snapshot = runtime_fixtures::makeFullProject(PlaneLayout::Ordinary);
    // Colors that pack well; frames 7 and 8 get the colors of frame 0, each in a buffer of
    // its own.
    for (std::size_t frame = 0; frame < snapshot.frames.size(); ++frame) {
        const std::size_t count = snapshot.frames[frame].colorizedSd.pixels.size();
        snapshot.frames[frame].colorizedSd.pixels =
            runs(count, static_cast<uint16_t>(frame == 7 || frame == 8 ? 0 : 100 * frame));
    }
    ProjectDocument document = ProjectDocument::fromSnapshot(std::move(snapshot));
    std::ostringstream before;
    std::string error;
    expect(legacy::writeLegacyCrom(before, document.toSnapshot(), error), "the cROM before");
    {
        const ProjectSnapshot shared = document.toSnapshot();
        expect(shared.frames[7].colorizedSd.pixels.storage() ==
                   shared.frames[0].colorizedSd.pixels.storage(),
               "identical blocks share one buffer once the document is built");
    }

    // A snapshot holds every block: nothing is packed.
    {
        const ProjectSnapshot held = document.toSnapshot();
        ProjectDocument::PackProgress progress;
        do {
            progress = document.packBlocks({}, std::chrono::seconds(10));
        } while (!progress.done);
        expect(progress.bytesFreed == 0 && !held.frames[0].originalSd.pixels.isPacked(),
               "blocks a snapshot holds are not packed");
    }
    const uint64_t revision = document.revision();
    const std::size_t heldBefore = document.heldBlockBytes();
    ProjectDocument::PackProgress progress = document.packBlocks({2}, std::chrono::seconds(10));
    expect(progress.done && progress.bytesFreed > 0 && document.heldBlockBytes() < heldBefore &&
               document.revision() == revision && !document.isDirty(),
           "packing frees memory and changes nothing of the project");
    const ProjectSnapshot after = document.toSnapshot();
    expect(after.frames[0].colorizedSd.pixels.isPacked() &&
               after.frames[7].colorizedSd.pixels.isPacked() &&
               !after.frames[2].colorizedSd.pixels.isPacked(),
           "shared blocks too, the frames to keep stay unpacked");
    std::ostringstream again;
    expect(legacy::writeLegacyCrom(again, after, error) && again.str() == before.str(),
           "the cROM of packed blocks is the same");
}

void runCompactStorageTests()
{
    testPackedArrays();
    testDocumentSharesAndPacksBlocks();
    testUniformArraysShareOneBuffer();
    testFirstWriteMaterialises();
    testCopiesShareUntilWritten();
    testCopyIsReadWhileOriginalIsWritten();
    testLoadedDefaultBlocksAreShared();
    testEditMaterialisesItsBlockAndUndoRestoresTheBytes();
    testUniformBlocksKeepTheirValue();
    testSaveSnapshotSharesTheDocumentsBlocks();
    testFileReaderStreamsWithProgress();
    testByteSourceAcrossBufferBoundaries();
    testWriteCancelsInTheMiddle();
    testFileWriterCancelAndProgress();
    testStructureUndoMemoryLimit();
}

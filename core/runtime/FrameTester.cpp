#include "runtime/FrameTester.h"

#include <utility>

#include "import/FrameDumpImport.h"
#include "import/FrameDumpReader.h"

namespace colorizing::runtime {

namespace {

std::string sizeText(render::Size size)
{
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

}  // namespace

std::vector<TesterFrame> testerFramesOfProject(const document::ProjectDocument& document,
                                               const std::vector<std::size_t>& frameIndices)
{
    std::vector<TesterFrame> frames;
    frames.reserve(frameIndices.size());
    for (std::size_t frameIndex : frameIndices) {
        frames.push_back(TesterFrame{document.originalFrame(frameIndex),
                                     document.frameDurationMs(frameIndex),
                                     frameIndex,
                                     0});
    }
    return frames;
}

TesterDump readTesterDump(std::istream& stream,
                          const document::ProjectDocument& document,
                          const TesterDumpProgress& progress)
{
    TesterDump dump;
    importing::FrameDumpReader reader(stream);
    const render::Size romSize = document.originalFrameSize();
    const uint32_t shades = document.colorCount();
    importing::DumpFrame frame;
    uint64_t reported = 0;
    while (true) {
        const importing::FrameDumpReader::Status status = reader.next(frame);
        if (status == importing::FrameDumpReader::Status::Error) {
            dump.frames.clear();
            dump.error = reader.error().text();
            return dump;
        }
        if (status == importing::FrameDumpReader::Status::End) {
            break;
        }
        if (dump.frames.empty() && !(reader.storedFrameSize() == romSize)) {
            dump.error = "the dump has " + sizeText(reader.dumpFrameSize()) +
                         " frames, the project " + sizeText(romSize);
            return dump;
        }
        if ((frame.usedShades >> shades) != 0) {
            dump.frames.clear();
            dump.error = "line " + std::to_string(frame.line) + ": the frame has shades above " +
                         std::to_string(shades - 1) + ", which the project's frames cannot have";
            return dump;
        }
        // A frame lasts until the next one; known once that is read.
        if (!dump.frames.empty()) {
            TesterFrame& previous = dump.frames.back();
            if (frame.timecode >= previous.timecode &&
                frame.timecode - previous.timecode <= importing::kMaxFrameDurationMs) {
                previous.durationMs = frame.timecode - previous.timecode;
            }
        }
        dump.frames.push_back(TesterFrame{std::move(frame.pixels),
                                          importing::kDefaultFrameDurationMs,
                                          std::nullopt,
                                          frame.timecode});
        frame = {};
        if (progress && reader.bytesRead() - reported >= (uint64_t{1} << 20)) {
            reported = reader.bytesRead();
            if (!progress(reported)) {
                dump.frames.clear();
                dump.cancelled = true;
                return dump;
            }
        }
    }
    if (dump.frames.empty()) {
        dump.error = "the dump has no frames";
    }
    dump.truncatedFrameLine = reader.truncatedFrameLine();
    return dump;
}

std::optional<std::size_t> findProjectFrame(const document::ProjectDocument& document,
                                            std::size_t frameIndex,
                                            const std::vector<TesterFrame>& frames,
                                            std::size_t after,
                                            bool underMask)
{
    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    const document::IndexedFrame* mask = nullptr;
    const uint8_t maskId = document.comparisonMaskId(frameIndex);
    if (underMask && maskId < document.comparisonMaskCount()) {
        mask = &document.comparisonMask(maskId);
    }
    const std::size_t count = frames.size();
    const std::size_t pixels = original.pixels.size();
    for (std::size_t step = 1; step <= count; ++step) {
        const std::size_t position = (after + step) % count;
        const document::IndexedFrame& candidate = frames[position].original;
        if (candidate.pixels.size() != pixels) {
            continue;
        }
        bool same = true;
        for (std::size_t i = 0; i < pixels && same; ++i) {
            // As CompareFrameToDump: a mask value above 0 leaves the pixel out.
            same = (mask && mask->pixels[i] > 0) || candidate.pixels[i] == original.pixels[i];
        }
        if (same) {
            return position;
        }
    }
    return std::nullopt;
}

}  // namespace colorizing::runtime

#include "runtime/RuntimePreview.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <thread>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ostream>
#include <set>
#include <stdexcept>
#include <streambuf>

#include <serum-decode.h>

#include "document/ProjectSnapshot.h"
#include "legacy/LegacyProjectSerializer.h"

namespace colorizing::runtime {

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

class VectorBuffer final : public std::streambuf {
public:
    explicit VectorBuffer(std::vector<uint8_t>& bytes)
        : m_bytes(bytes)
    {
    }

protected:
    std::streamsize xsputn(const char* data, std::streamsize count) override
    {
        m_bytes.insert(m_bytes.end(), data, data + count);
        return count;
    }

    int_type overflow(int_type character) override
    {
        if (!traits_type::eq_int_type(character, traits_type::eof())) {
            m_bytes.push_back(static_cast<uint8_t>(traits_type::to_char_type(character)));
        }
        return traits_type::not_eof(character);
    }

private:
    std::vector<uint8_t>& m_bytes;
};

uint8_t requestFlags(PlaneRequest request)
{
    switch (request) {
        case PlaneRequest::Rows32:
            return FLAG_REQUEST_32P_FRAMES;
        case PlaneRequest::Rows64:
            return FLAG_REQUEST_64P_FRAMES;
        case PlaneRequest::Both:
            break;
    }
    return FLAG_REQUEST_32P_FRAMES | FLAG_REQUEST_64P_FRAMES;
}

RuntimePlane copyPlane(const uint16_t* pixels, uint32_t width, int rows, const uint16_t* rotations)
{
    RuntimePlane plane;
    plane.image.size = render::Size{static_cast<int>(width), rows};
    plane.image.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * rows);
    for (std::size_t slot = 0; rotations && slot < plane.rotations.size(); ++slot) {
        plane.rotations[slot].length = rotations[slot * MAX_LENGTH_COLOR_ROTATION];
        plane.rotations[slot].delayMs = rotations[slot * MAX_LENGTH_COLOR_ROTATION + 1];
    }
    return plane;
}

void logLine(const char* format, va_list arguments, const void* userData)
{
    auto* lines = static_cast<std::vector<std::string>*>(const_cast<void*>(userData));
    char text[512];
    std::vsnprintf(text, sizeof(text), format, arguments);
    lines->emplace_back(text);
}

}  // namespace

std::vector<PlaneRequest> planeRequestsFor(const render::ProjectGeometry& geometry)
{
    if (geometry.layout() == render::PlaneLayout::Ordinary) {
        return {PlaneRequest::Rows32, PlaneRequest::Rows64};
    }
    return {PlaneRequest::Rows64};
}

PlaneRequest effectivePlaneRequest(const render::ProjectGeometry& geometry, PlaneRequest request)
{
    return geometry.layout() == render::PlaneLayout::Ordinary ? request : PlaneRequest::Rows64;
}

bool RuntimePlane::rotates() const
{
    return std::any_of(rotations.begin(), rotations.end(), [](const RuntimeRotationSlot& slot) {
        return slot.length > 0 && slot.delayMs > 0;
    });
}

const RuntimePlane* RuntimeFrame::plane(PlaneRequest rows) const
{
    if (rows == PlaneRequest::Rows32) {
        return rows32 ? &*rows32 : nullptr;
    }
    if (rows == PlaneRequest::Rows64) {
        return rows64 ? &*rows64 : nullptr;
    }
    return nullptr;
}

namespace {

bool hasExtraDynamicContent(const document::SpriteRecord& sprite)
{
    return std::any_of(sprite.dynamicMaskHd.pixels.begin(),
                       sprite.dynamicMaskHd.pixels.end(),
                       [](uint8_t set) { return set != 255; });
}

// libserum keeps the extra-plane dynamic data of sprite N only if frame N has extra-plane
// content (its loader reads those two blocks with the frame flags as parent). That data is
// drawn in the 256x64-original layout alone. A subset numbers frames and sprites anew, so the
// rule is applied here by the document's numbers: a sprite that loses the data in the whole
// project gets it cleared, and one that keeps it gets a frame with extra content at its new
// number, placed in front of the real frames and matching no ROM frame.
void keepExtraSpriteDynamicsAsInProject(const document::ProjectDocument& document,
                                        document::ProjectSnapshot& snapshot,
                                        document::SnapshotSubsetMap& map)
{
    if (snapshot.geometry.layout() != render::PlaneLayout::NativeExtra) {
        return;
    }
    const render::Size spriteSize{static_cast<int>(document::kMaxSpriteWidth),
                                  static_cast<int>(document::kMaxSpriteHeight)};
    std::size_t paddingFrames = 0;
    for (std::size_t sprite = 0; sprite < snapshot.sprites.size(); ++sprite) {
        document::SpriteRecord& record = snapshot.sprites[sprite];
        if (!hasExtraDynamicContent(record)) {
            continue;
        }
        const std::size_t documentSprite = map.sprites[sprite];
        if (documentSprite < document.frameCount() && document.hasHdFrame(documentSprite)) {
            paddingFrames = sprite + 1;
        } else {
            record.dynamicMaskHd = document::makeIndexedFrame(spriteSize, 255);
            std::fill(record.dynamicColorsHd.begin(), record.dynamicColorsHd.end(), uint16_t{0});
        }
    }
    if (paddingFrames == 0) {
        return;
    }
    const render::Size romSize = snapshot.geometry.romFrameSize();
    // A shape mode value no frame of the project has: with it these frames are a comparison
    // bucket of their own, which libserum's search looks into without finding anything and
    // which therefore changes nothing about the order it meets the other buckets in. Any
    // value but 1 hashes without shape mode.
    std::array<bool, 256> usedShapeModes{};
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        usedShapeModes[document.shapeComparisonMode(frame)] = true;
    }
    uint8_t paddingShapeMode = 2;
    while (paddingShapeMode < 255 && usedShapeModes[paddingShapeMode]) {
        ++paddingShapeMode;
    }
    std::vector<document::ProjectFrameSnapshot> frames;
    std::vector<std::size_t> frameMap;
    for (std::size_t padding = 0; padding < paddingFrames; ++padding) {
        document::ProjectFrameSnapshot frame;
        // Every shade at its highest value but for one pixel per frame: no ROM draws this.
        frame.originalSd = document::makeIndexedFrame(
            romSize, static_cast<uint8_t>(snapshot.colorCount - 1));
        frame.originalSd.pixels[padding] = 0;
        frame.colorizedSd =
            document::makeRgb565Frame(snapshot.geometry.planeSize(render::Resolution::Sd));
        frame.colorizedHd =
            document::makeRgb565Frame(snapshot.geometry.planeSize(render::Resolution::Hd));
        frame.comparisonMaskId = 255;
        frame.shapeComparisonMode = paddingShapeMode;
        frame.triggerId = document::kNoTriggerId;
        frames.push_back(std::move(frame));
        frameMap.push_back(kNoDocumentFrame);
    }
    for (std::size_t i = 0; i < snapshot.frames.size(); ++i) {
        frames.push_back(std::move(snapshot.frames[i]));
        frameMap.push_back(map.frames[i]);
    }
    snapshot.frames = std::move(frames);
    map.frames = std::move(frameMap);
}

}  // namespace

RuntimeSubset buildRuntimeSubset(const document::ProjectDocument& document,
                                 const std::vector<std::size_t>& frameIndices)
{
    RuntimeSubset subset;
    subset.geometry = document.geometry();
    Clock::time_point start = Clock::now();
    document::ProjectSnapshot snapshot = document.toSubsetSnapshot(frameIndices, &subset.map);
    keepExtraSpriteDynamicsAsInProject(document, snapshot, subset.map);
    subset.subsetMs = millisecondsSince(start);

    start = Clock::now();
    VectorBuffer buffer(subset.crom);
    std::ostream stream(&buffer);
    std::string errorMessage;
    if (!legacy::writeLegacyCrom(stream, snapshot, errorMessage)) {
        throw std::runtime_error(errorMessage);
    }
    subset.serializeMs = millisecondsSince(start);

    subset.originals.reserve(snapshot.frames.size());
    for (document::ProjectFrameSnapshot& frame : snapshot.frames) {
        subset.originals.push_back(std::move(frame.originalSd));
    }
    return subset;
}

namespace {

// The cROM between the thread that writes it and libserum, which reads it: a bounded buffer,
// so that neither holds the whole cROM.
class CromPipe {
public:
    // Blocks while the buffer is full; false once the reader stopped.
    bool write(const char* data, std::size_t size)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (size > 0) {
            m_changed.wait(lock, [this]() { return m_closed || m_bytes.size() < kCapacity; });
            if (m_closed) {
                return false;
            }
            const std::size_t count = std::min(size, kCapacity - m_bytes.size());
            m_bytes.insert(m_bytes.end(), data, data + count);
            data += count;
            size -= count;
            m_changed.notify_all();
        }
        return true;
    }
    // The writer is done, or gave up: the reader gets what is left, then 0.
    void finishWriting(bool ok)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_written = true;
        m_writerFailedFirst = !ok && !m_closed;
        m_changed.notify_all();
    }
    // The writer failed while the reader still read: the writer's error is the reason.
    bool writerFailedFirst()
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_writerFailedFirst;
    }
    // Blocks until there are bytes or the writer is done; 0 at the end.
    std::size_t read(uint8_t* buffer, std::size_t size)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_changed.wait(lock, [this]() { return m_written || !m_bytes.empty(); });
        const std::size_t count = std::min(size, m_bytes.size());
        std::copy_n(m_bytes.begin(), count, buffer);
        m_bytes.erase(m_bytes.begin(), m_bytes.begin() + static_cast<std::ptrdiff_t>(count));
        m_changed.notify_all();
        return count;
    }
    // The reader stopped: a blocked writer returns.
    void closeReading()
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
        m_changed.notify_all();
    }

private:
    static constexpr std::size_t kCapacity = 4u << 20;
    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::deque<char> m_bytes;
    bool m_written = false;
    bool m_closed = false;
    bool m_writerFailedFirst = false;
};

// writeLegacyCrom() into the pipe.
class CromPipeBuffer final : public std::streambuf {
public:
    explicit CromPipeBuffer(CromPipe& pipe) : m_pipe(pipe) {}

protected:
    std::streamsize xsputn(const char* data, std::streamsize count) override
    {
        return m_pipe.write(data, static_cast<std::size_t>(count)) ? count : 0;
    }
    int_type overflow(int_type character) override
    {
        if (traits_type::eq_int_type(character, traits_type::eof())) {
            return traits_type::not_eof(character);
        }
        const char byte = traits_type::to_char_type(character);
        return m_pipe.write(&byte, 1) ? character : traits_type::eof();
    }

private:
    CromPipe& m_pipe;
};

size_t readCromPipe(void* userData, uint8_t* buffer, size_t size)
{
    return static_cast<CromPipe*>(userData)->read(buffer, size);
}

}  // namespace

struct RuntimePreview::Impl {
    Serum_Frame_Struc* serum = nullptr;
    // See setSceneFile().
    std::string sceneFile;
    std::size_t frameCount = 0;
    // Loaded frame ID -> document frame; empty for a cROM opened as it is.
    std::vector<std::size_t> frameMap;
    std::vector<document::IndexedFrame> originals;
    render::Size romFrameSize;
    std::vector<uint8_t> romFrame;
    RuntimeFrame shown;

    // The search rule of the document last asked about, see RuntimePreview::identification().
    std::shared_ptr<const FrameIdentification> identificationRule;
    // A sequence opened from a document, see RuntimePreview::openSequence().
    bool sequence = false;
    // The frames the loaded window plays.
    std::vector<std::size_t> windowFrames;
    // libserum's identification state in document terms: the frame its search starts at (the
    // frame identified last) and the ROM frame that was fed when it was identified.
    std::size_t searchStart = 0;
    std::optional<document::IndexedFrame> lastIdentifiedInput;
    std::size_t cromBytes = 0;

    [[nodiscard]] std::shared_ptr<const FrameIdentification> identification(
        const document::ProjectDocument& document);
    [[nodiscard]] bool loadWindow(const document::ProjectDocument& document,
                                  const std::vector<std::size_t>& frameIndices,
                                  PlaneRequest request,
                                  std::size_t maxWindowFrames,
                                  std::string* errorMessage);
    // Releases the colorization and keeps the sequence's identification state.
    void unloadColorization();

    // loader loads the colorization with the scene file (or nullptr) and the flags.
    [[nodiscard]] bool load(const std::function<Serum_Frame_Struc*(const char*, uint8_t)>& loader,
                            const render::ProjectGeometry& geometry,
                            std::size_t frames,
                            PlaneRequest request,
                            std::string* errorMessage);
    [[nodiscard]] bool load(const std::vector<uint8_t>& crom,
                            const render::ProjectGeometry& geometry,
                            std::size_t frames,
                            PlaneRequest request,
                            std::string* errorMessage);
    [[nodiscard]] bool loadProject(const document::ProjectDocument& document,
                                   PlaneRequest request,
                                   std::string* errorMessage);
    [[nodiscard]] bool load(RuntimeSubset subset, PlaneRequest request, std::string* errorMessage);
    void unload();
    void readPlanes(RuntimeFrame& frame) const;
    [[nodiscard]] RuntimeFrame colorize(const document::IndexedFrame& original);
};

bool RuntimePreview::Impl::load(const std::vector<uint8_t>& crom,
                                const render::ProjectGeometry& geometry,
                                std::size_t frames,
                                PlaneRequest request,
                                std::string* errorMessage)
{
    return load(
        [&crom](const char* scenes, uint8_t flags) {
            return Serum_LoadFromMemoryWithScenes(crom.data(), crom.size(), scenes, flags);
        },
        geometry, frames, request, errorMessage);
}

bool RuntimePreview::Impl::loadProject(const document::ProjectDocument& document,
                                       PlaneRequest request,
                                       std::string* errorMessage)
{
    // The cROM is written on a thread of its own into a bounded pipe that libserum reads:
    // it is never in memory as a whole.
    document::ProjectSnapshot snapshot = document.toSnapshot();
    const uint64_t size = legacy::legacyCromByteCount(snapshot);
    CromPipe pipe;
    std::string writeError;
    std::thread writer([&pipe, &writeError, snapshot = std::move(snapshot)]() {
        CromPipeBuffer buffer(pipe);
        std::ostream stream(&buffer);
        bool ok = false;
        try {
            ok = legacy::writeLegacyCrom(stream, snapshot, writeError) && stream.flush();
        } catch (const std::exception& ex) {
            writeError = ex.what();
        }
        if (!ok && writeError.empty()) {
            writeError = "the cROM could not be written";
        }
        pipe.finishWriting(ok);
    });
    std::string loadError;
    const bool loaded = load(
        [&pipe, size](const char* scenes, uint8_t flags) {
            return Serum_LoadFromCallback(readCromPipe, &pipe, static_cast<size_t>(size), scenes,
                                          flags);
        },
        document.geometry(), document.frameCount(), request, &loadError);
    pipe.closeReading();
    writer.join();
    if (!loaded && errorMessage) {
        *errorMessage = pipe.writerFailedFirst() ? writeError : loadError;
    }
    return loaded;
}

bool RuntimePreview::Impl::load(
    const std::function<Serum_Frame_Struc*(const char*, uint8_t)>& loader,
    const render::ProjectGeometry& geometry,
    std::size_t frames,
    PlaneRequest request,
    std::string* errorMessage)
{
    unload();
    // An unknown frame stays unknown: no grey fallback picture after a number of frames or a
    // time, both of which are process-wide settings of libserum.
    Serum_SetMaximumUnknownFramesToSkip(0);
    Serum_SetIgnoreUnknownFramesTimeout(0);

    std::vector<std::string> log;
    Serum_SetLogCallback(logLine, &log);
    serum = loader(sceneFile.empty() ? nullptr : sceneFile.c_str(),
                   requestFlags(effectivePlaneRequest(geometry, request)));
    const char* lastError = Serum_GetLastErrorMessage();
    std::string message = lastError ? lastError : "";
    Serum_SetLogCallback(nullptr, nullptr);
    if (serum) {
        romFrameSize = geometry.romFrameSize();
        frameCount = frames;
        return true;
    }
    if (message.empty()) {
        message = "libserum could not load the colorization";
    }
    // The line before the summary says what libserum found wrong, e.g. a frame size.
    if (log.size() >= 2) {
        message += " (" + log[log.size() - 2] + ")";
    }
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

bool RuntimePreview::Impl::load(RuntimeSubset subset,
                                PlaneRequest request,
                                std::string* errorMessage)
{
    if (!load(subset.crom, subset.geometry, subset.map.frames.size(), request, errorMessage)) {
        return false;
    }
    cromBytes = subset.crom.size();
    frameMap = std::move(subset.map.frames);
    originals = std::move(subset.originals);
    return true;
}

void RuntimePreview::Impl::unloadColorization()
{
    if (serum) {
        Serum_Dispose();
    }
    serum = nullptr;
    frameCount = 0;
    frameMap.clear();
    originals.clear();
    windowFrames.clear();
    shown = RuntimeFrame{};
}

void RuntimePreview::Impl::unload()
{
    unloadColorization();
    sequence = false;
    searchStart = 0;
    lastIdentifiedInput.reset();
}

std::shared_ptr<const FrameIdentification> RuntimePreview::Impl::identification(
    const document::ProjectDocument& document)
{
    if (!identificationRule ||
        identificationRule->revision() != document.identificationRevision()) {
        identificationRule = std::make_shared<const FrameIdentification>(document);
    }
    return identificationRule;
}

bool RuntimePreview::Impl::loadWindow(const document::ProjectDocument& document,
                                      const std::vector<std::size_t>& frameIndices,
                                      PlaneRequest request,
                                      std::size_t maxWindowFrames,
                                      std::string* errorMessage)
{
    const std::shared_ptr<const FrameIdentification> rule = identification(document);
    if (searchStart >= document.frameCount()) {
        searchStart = 0;
    }
    // The frames to play and with them every frame that takes part in identifying them.
    std::set<std::size_t> starts{searchStart};
    std::vector<std::size_t> played;
    std::vector<std::size_t> loaded;
    for (std::size_t frameIndex : frameIndices) {
        if (frameIndex >= document.frameCount()) {
            throw std::out_of_range("frame index is out of range");
        }
        if (std::find(played.begin(), played.end(), frameIndex) != played.end()) {
            break;
        }
        std::set<std::size_t> grown = starts;
        const std::vector<std::size_t> matching = rule->matches(document.originalFrame(frameIndex));
        grown.insert(matching.begin(), matching.end());
        grown.insert(frameIndex);
        std::vector<std::size_t> candidates = rule->candidatesOf(grown);
        if (!played.empty() && candidates.size() > kSequenceLoadedFrames) {
            break;
        }
        starts = std::move(grown);
        loaded = std::move(candidates);
        played.push_back(frameIndex);
        if (played.size() >= std::max<std::size_t>(maxWindowFrames, 1)) {
            break;
        }
    }
    if (played.empty()) {
        throw std::out_of_range("no frame to play");
    }
    // File order, starting with the frame the search starts at: libserum's search starts
    // at the first frame of a colorization it has just loaded.
    std::rotate(loaded.begin(),
                std::lower_bound(loaded.begin(), loaded.end(), searchStart),
                loaded.end());

    RuntimeSubset subset = buildRuntimeSubset(document, loaded);
    // load() releases what is loaded; the identification state is kept across it.
    const std::size_t start = searchStart;
    const std::optional<document::IndexedFrame> replay = lastIdentifiedInput;
    if (!load(std::move(subset), request, errorMessage)) {
        unload();
        return false;
    }
    sequence = true;
    searchStart = start;
    lastIdentifiedInput = replay;
    windowFrames = std::move(played);
    if (replay) {
        // The ROM frame identified last, once more: libserum identifies it as the frame the
        // colorization starts with, which leaves its search exactly where it was.
        romFrame.assign(replay->pixels.begin(), replay->pixels.end());
        const uint32_t result = Serum_Colorize(romFrame.data());
        if (result != IDENTIFY_NO_FRAME && result != IDENTIFY_SAME_FRAME) {
            RuntimeFrame frame;
            frame.identified = true;
            frame.frameIndex = start;
            frame.nextRotationMs = result & 0xffff;
            readPlanes(frame);
            shown = std::move(frame);
        }
    }
    return true;
}

void RuntimePreview::Impl::readPlanes(RuntimeFrame& frame) const
{
    frame.rows32.reset();
    frame.rows64.reset();
    if ((serum->flags & FLAG_RETURNED_32P_FRAME_OK) && serum->frame32 && serum->width32 > 0) {
        frame.rows32 = copyPlane(serum->frame32, serum->width32, 32, serum->rotations32);
    }
    if ((serum->flags & FLAG_RETURNED_64P_FRAME_OK) && serum->frame64 && serum->width64 > 0) {
        frame.rows64 = copyPlane(serum->frame64, serum->width64, 64, serum->rotations64);
    }
}

RuntimeFrame RuntimePreview::Impl::colorize(const document::IndexedFrame& original)
{
    RuntimeFrame frame;
    if (!serum) {
        frame.error = "no colorization is loaded";
        return frame;
    }
    if (original.size != romFrameSize ||
        original.pixels.size() != static_cast<std::size_t>(romFrameSize.width) *
                                      static_cast<std::size_t>(romFrameSize.height)) {
        frame.error = "the frame does not have the size of the ROM frame";
        return frame;
    }

    const bool extraPlaneAvailable = (serum->flags & FLAG_RETURNED_EXTRA_AVAILABLE) != 0;
    // What libserum's search rule gives for this ROM frame from where the search stands.
    std::optional<std::size_t> expected;
    if (sequence && identificationRule) {
        expected = identificationRule->identify(original, searchStart);
    }
    romFrame.assign(original.pixels.begin(), original.pixels.end());
    const Clock::time_point start = Clock::now();
    const uint32_t result = Serum_Colorize(romFrame.data());
    frame.cost.colorizeMs = millisecondsSince(start);
    const char* lastError = Serum_GetLastErrorMessage();
    if (lastError && lastError[0] != '\0') {
        frame.error = lastError;
        return frame;
    }
    // The frame libserum reports. A fallback picture of an unknown frame carries a frame ID
    // past every frame. For a frame it identifies and does not render it returns "no frame"
    // and still reports the ID.
    std::optional<std::size_t> reported;
    if (result != IDENTIFY_SAME_FRAME && serum->frameID < frameCount) {
        const std::size_t documentFrame =
            frameMap.empty() ? serum->frameID : frameMap[serum->frameID];
        if (documentFrame != kNoDocumentFrame) {
            reported = documentFrame;
        }
    }
    // libserum says "same frame" both when it identified the frame it identified before and
    // when it identified another one whose picture is the one on display; it does not tell
    // which. The search rule does.
    const std::optional<std::size_t> identified =
        result == IDENTIFY_SAME_FRAME && sequence ? expected : reported;
    if (sequence && identified) {
        searchStart = *identified;
        lastIdentifiedInput = original;
    }

    if (result == IDENTIFY_SAME_FRAME) {
        frame = shown;
        frame.unchanged = true;
        frame.skipped = false;
        if (sequence) {
            frame.identified = identified.has_value();
            frame.frameIndex = identified;
        }
        frame.expectedFrameIndex = expected;
        frame.triggerId = document::kNoTriggerId;
        frame.cost = RuntimeCost{};
        frame.cost.colorizeMs = millisecondsSince(start);
        return frame;
    }
    frame.extraPlaneAvailable = extraPlaneAvailable;
    frame.expectedFrameIndex = expected;
    frame.identified = identified.has_value();
    frame.frameIndex = identified;
    if (result == IDENTIFY_NO_FRAME) {
        frame.skipped = identified.has_value();
        return frame;
    }
    frame.triggerId = serum->triggerID;
    frame.nextRotationMs = result & 0xffff;
    readPlanes(frame);
    shown = frame;
    return frame;
}

RuntimePreview& RuntimePreview::instance()
{
    static RuntimePreview preview;
    return preview;
}

RuntimePreview::RuntimePreview()
    : m_impl(std::make_unique<Impl>())
{
}

RuntimePreview::~RuntimePreview()
{
    m_impl->unload();
}

RuntimeFrame RuntimePreview::renderFrame(const document::ProjectDocument& document,
                                         std::size_t frameIndex,
                                         PlaneRequest request)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    RuntimeFrame frame;
    RuntimeCost cost;
    try {
        RuntimeSubset subset = buildRuntimeSubset(document, {frameIndex});
        cost.subsetMs = subset.subsetMs;
        cost.serializeMs = subset.serializeMs;
        cost.cromBytes = subset.crom.size();
        const Clock::time_point start = Clock::now();
        std::string errorMessage;
        if (!m_impl->load(std::move(subset), request, &errorMessage)) {
            frame.error = errorMessage;
            return frame;
        }
        cost.loadMs = millisecondsSince(start);
        const document::IndexedFrame original = std::move(m_impl->originals.back());
        frame = m_impl->colorize(original);
    } catch (const std::exception& ex) {
        frame = RuntimeFrame{};
        frame.error = ex.what();
    }
    m_impl->unload();
    cost.colorizeMs = frame.cost.colorizeMs;
    frame.cost = cost;
    return frame;
}

bool RuntimePreview::openSequence(const document::ProjectDocument& document,
                                  const std::vector<std::size_t>& frameIndices,
                                  PlaneRequest request,
                                  std::string* errorMessage,
                                  std::size_t windowFrames)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_impl->unload();
    try {
        return m_impl->loadWindow(document, frameIndices, request, windowFrames, errorMessage);
    } catch (const std::exception& ex) {
        m_impl->unload();
        if (errorMessage) {
            *errorMessage = ex.what();
        }
        return false;
    }
}

bool RuntimePreview::continueSequence(const document::ProjectDocument& document,
                                      const std::vector<std::size_t>& frameIndices,
                                      PlaneRequest request,
                                      std::string* errorMessage,
                                      std::size_t windowFrames)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_impl->sequence) {
        m_impl->unload();
    }
    try {
        return m_impl->loadWindow(document, frameIndices, request, windowFrames, errorMessage);
    } catch (const std::exception& ex) {
        m_impl->unload();
        if (errorMessage) {
            *errorMessage = ex.what();
        }
        return false;
    }
}

std::size_t RuntimePreview::sequenceLoadedFrameCount() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl->serum ? m_impl->frameCount : 0;
}

std::size_t RuntimePreview::sequenceCromBytes() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl->serum ? m_impl->cromBytes : 0;
}

std::shared_ptr<const FrameIdentification> RuntimePreview::identification(
    const document::ProjectDocument& document)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl->identification(document);
}

bool RuntimePreview::openCrom(const std::vector<uint8_t>& crom,
                              const render::ProjectGeometry& geometry,
                              std::size_t frameCount,
                              PlaneRequest request,
                              std::string* errorMessage)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl->load(crom, geometry, frameCount, request, errorMessage);
}

bool RuntimePreview::openProject(const document::ProjectDocument& document,
                                 PlaneRequest request,
                                 std::string* errorMessage)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl->loadProject(document, request, errorMessage);
}

bool RuntimePreview::isOpen() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl->serum != nullptr;
}

bool RuntimePreview::sequenceHasFrame(std::size_t frameIndex) const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl->serum != nullptr &&
           std::find(m_impl->windowFrames.begin(), m_impl->windowFrames.end(), frameIndex) !=
               m_impl->windowFrames.end();
}

RuntimeFrame RuntimePreview::renderSequenceFrame(std::size_t frameIndex)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = std::find(m_impl->frameMap.begin(), m_impl->frameMap.end(), frameIndex);
    if (!m_impl->serum || found == m_impl->frameMap.end()) {
        RuntimeFrame frame;
        frame.error = "the frame is not part of the loaded sequence";
        return frame;
    }
    return m_impl->colorize(
        m_impl->originals[static_cast<std::size_t>(found - m_impl->frameMap.begin())]);
}

RuntimeFrame RuntimePreview::colorize(const document::IndexedFrame& original)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl->colorize(original);
}

RuntimeFrame RuntimePreview::rotate()
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_impl->serum) {
        RuntimeFrame frame;
        frame.error = "no colorization is loaded";
        return frame;
    }
    const uint32_t result = Serum_Rotate();
    RuntimeFrame& shown = m_impl->shown;
    const bool rotated = (result & (FLAG_RETURNED_V2_ROTATED32 | FLAG_RETURNED_V2_ROTATED64)) != 0;
    if (rotated) {
        m_impl->readPlanes(shown);
    }
    shown.nextRotationMs = result & 0xffff;
    RuntimeFrame frame = shown;
    frame.unchanged = !rotated;
    frame.triggerId = document::kNoTriggerId;
    frame.cost = RuntimeCost{};
    return frame;
}

void RuntimePreview::setSceneFile(const std::string& path)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (m_impl->sceneFile == path) {
        return;
    }
    m_impl->unload();
    m_impl->sceneFile = path;
}

void RuntimePreview::close()
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_impl->unload();
}

}  // namespace colorizing::runtime

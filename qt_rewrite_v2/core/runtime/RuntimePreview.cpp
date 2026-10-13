#include "runtime/RuntimePreview.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ostream>
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

struct RuntimePreview::Impl {
    Serum_Frame_Struc* serum = nullptr;
    std::size_t frameCount = 0;
    // Loaded frame ID -> document frame; empty for a cROM opened as it is.
    std::vector<std::size_t> frameMap;
    std::vector<document::IndexedFrame> originals;
    render::Size romFrameSize;
    std::vector<uint8_t> romFrame;
    RuntimeFrame shown;

    [[nodiscard]] bool load(const std::vector<uint8_t>& crom,
                            const render::ProjectGeometry& geometry,
                            std::size_t frames,
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
    unload();
    // An unknown frame stays unknown: no grey fallback picture after a number of frames or a
    // time, both of which are process-wide settings of libserum.
    Serum_SetMaximumUnknownFramesToSkip(0);
    Serum_SetIgnoreUnknownFramesTimeout(0);

    std::vector<std::string> log;
    Serum_SetLogCallback(logLine, &log);
    serum = Serum_LoadFromMemory(crom.data(), crom.size(),
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
    frameMap = std::move(subset.map.frames);
    originals = std::move(subset.originals);
    return true;
}

void RuntimePreview::Impl::unload()
{
    if (serum) {
        Serum_Dispose();
    }
    serum = nullptr;
    frameCount = 0;
    frameMap.clear();
    originals.clear();
    shown = RuntimeFrame{};
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
    romFrame = original.pixels;
    const Clock::time_point start = Clock::now();
    const uint32_t result = Serum_Colorize(romFrame.data());
    frame.cost.colorizeMs = millisecondsSince(start);
    const char* lastError = Serum_GetLastErrorMessage();
    if (lastError && lastError[0] != '\0') {
        frame.error = lastError;
        return frame;
    }

    if (result == IDENTIFY_SAME_FRAME) {
        frame = shown;
        frame.unchanged = true;
        frame.triggerId = document::kNoTriggerId;
        frame.cost = RuntimeCost{};
        frame.cost.colorizeMs = millisecondsSince(start);
        return frame;
    }
    frame.extraPlaneAvailable = extraPlaneAvailable;
    if (result == IDENTIFY_NO_FRAME) {
        return frame;
    }
    // A fallback picture of an unknown frame carries a frame ID past every frame.
    const uint32_t frameId = serum->frameID;
    if (frameId < frameCount) {
        const std::size_t documentFrame = frameMap.empty() ? frameId : frameMap[frameId];
        if (documentFrame != kNoDocumentFrame) {
            frame.identified = true;
            frame.frameIndex = documentFrame;
        }
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
                                  std::string* errorMessage)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    try {
        return m_impl->load(buildRuntimeSubset(document, frameIndices), request, errorMessage);
    } catch (const std::exception& ex) {
        m_impl->unload();
        if (errorMessage) {
            *errorMessage = ex.what();
        }
        return false;
    }
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

bool RuntimePreview::isOpen() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl->serum != nullptr;
}

bool RuntimePreview::sequenceHasFrame(std::size_t frameIndex) const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_impl->serum != nullptr &&
           std::find(m_impl->frameMap.begin(), m_impl->frameMap.end(), frameIndex) !=
               m_impl->frameMap.end();
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

void RuntimePreview::close()
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_impl->unload();
}

}  // namespace colorizing::runtime

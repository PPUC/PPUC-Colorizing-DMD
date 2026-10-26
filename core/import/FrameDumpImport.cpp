#include "import/FrameDumpImport.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <utility>

#include "import/DumpFile.h"
#include "legacy/LegacyProjectSerializer.h"
#include "platform/Utf8Path.h"

namespace colorizing::importing {

namespace {

constexpr std::size_t kNotKept = static_cast<std::size_t>(-1);
constexpr std::size_t kProgressFrameInterval = 32;

int shadeCount(uint16_t usedShades)
{
    int count = 0;
    for (; usedShades != 0; usedShades = static_cast<uint16_t>(usedShades & (usedShades - 1))) {
        ++count;
    }
    return count;
}

std::string sizeText(render::Size size)
{
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

DumpImportResult failed(DumpImportResult result, std::string message, std::size_t line = 0)
{
    result.ok = false;
    result.frames.clear();
    result.error.message = std::move(message);
    result.error.line = line;
    return result;
}

}  // namespace

DumpImportOptions clampedOptions(DumpImportOptions options)
{
    options.shortFrameLimitMs =
        std::clamp(options.shortFrameLimitMs, kMinShortFrameLimitMs, kMaxShortFrameLimitMs);
    options.colorLimit = std::clamp(options.colorLimit, kMinColorLimit, kMaxColorLimit);
    options.transitionalLimitMs = std::clamp(options.transitionalLimitMs, kMinTransitionalLimitMs,
                                             kMaxTransitionalLimitMs);
    return options;
}

bool isTransitionalFrame(const document::IndexedFrame& previous,
                         const document::IndexedFrame& frame,
                         const document::IndexedFrame& next)
{
    if (previous.size != frame.size || next.size != frame.size ||
        frame.pixels == previous.pixels || frame.pixels == next.pixels ||
        previous.pixels == next.pixels) {
        return false;
    }
    // The shade a pixel going from a to c has in the frame, per (a, c); -1: none seen yet.
    std::array<int16_t, 16 * 16> mix;
    mix.fill(-1);
    bool between = false;
    const std::size_t count = frame.pixels.size();
    const uint8_t* before = previous.pixels.data();
    const uint8_t* pixels = frame.pixels.data();
    const uint8_t* after = next.pixels.data();
    for (std::size_t index = 0; index < count; ++index) {
        const uint8_t a = before[index] & 0x0f;
        const uint8_t b = pixels[index] & 0x0f;
        const uint8_t c = after[index] & 0x0f;
        if (a == c) {
            if (b != a) {
                return false;
            }
            continue;
        }
        int16_t& seen = mix[a * 16 + c];
        if (seen < 0) {
            seen = b;
            between = between || (b > std::min(a, c) && b < std::max(a, c));
        } else if (seen != b) {
            return false;
        }
    }
    return between;
}

ProjectFrameMatcher::ProjectFrameMatcher(const document::ProjectDocument& document,
                                         bool useMasksAndShapeModes)
    : m_document(document)
{
    for (std::size_t frameIndex = 0; frameIndex < document.frameCount(); ++frameIndex) {
        const document::IndexedFrame& original = document.originalFrame(frameIndex);
        const uint8_t storedMaskId = document.comparisonMaskId(frameIndex);
        const uint8_t maskId = storedMaskId < document.comparisonMaskCount()
                                   ? storedMaskId
                                   : document::kNoComparisonMaskId;
        // Only the value 1 is shape mode to the hash, as in the serializer.
        const bool shapeMode = document.shapeComparisonMode(frameIndex) == 1;
        if (!useMasksAndShapeModes ||
            (maskId == document::kNoComparisonMaskId && !shapeMode)) {
            m_plainFrames.emplace(legacy::legacyFrameHash(original, nullptr, false), frameIndex);
            continue;
        }
        auto rule = std::find_if(m_rules.begin(), m_rules.end(), [&](const Rule& candidate) {
            return candidate.maskId == maskId && candidate.shapeMode == shapeMode;
        });
        if (rule == m_rules.end()) {
            m_rules.push_back(Rule{maskId, shapeMode, {}});
            rule = m_rules.end() - 1;
        }
        const document::IndexedFrame* mask = maskId == document::kNoComparisonMaskId
                                                 ? nullptr
                                                 : &document.comparisonMask(maskId);
        rule->frames.emplace(legacy::legacyFrameHash(original, mask, shapeMode), frameIndex);
    }
}

ProjectFrameMatcher::Match ProjectFrameMatcher::match(const document::IndexedFrame& frame,
                                                      uint32_t plainHash,
                                                      bool* collision) const
{
    const auto plain = m_plainFrames.find(plainHash);
    if (plain != m_plainFrames.end()) {
        if (collision) {
            *collision = m_document.originalFrame(plain->second).pixels != frame.pixels;
        }
        return Match::Identical;
    }
    for (const Rule& rule : m_rules) {
        const document::IndexedFrame* mask = rule.maskId == document::kNoComparisonMaskId
                                                 ? nullptr
                                                 : &m_document.comparisonMask(rule.maskId);
        if (rule.frames.count(legacy::legacyFrameHash(frame, mask, rule.shapeMode)) > 0) {
            return Match::MaskOrShape;
        }
    }
    return Match::None;
}

DumpImportResult importFrameDump(std::istream& stream,
                                 const DumpImportOptions& requestedOptions,
                                 const document::ProjectDocument* project,
                                 const DumpProgress& progress)
{
    const DumpImportOptions options = clampedOptions(requestedOptions);
    DumpImportResult result;
    FrameDumpReader reader(stream);
    std::optional<ProjectFrameMatcher> matcher;
    // Hash of every frame that passed the filters -> its place in result.frames, or kNotKept
    // for one the project has already.
    std::unordered_map<uint32_t, std::size_t> seen;
    uint16_t usedShades = 0;

    // previous, next: the frames read before and after it, if any.
    const auto take = [&](DumpFrame& frame, const document::IndexedFrame* previous,
                          const DumpFrame* next) {
        const std::optional<uint32_t> nextTimecode =
            next ? std::optional<uint32_t>(next->timecode) : std::nullopt;
        uint32_t durationMs = kDefaultFrameDurationMs;
        bool measured = false;
        if (nextTimecode && *nextTimecode >= frame.timecode &&
            *nextTimecode - frame.timecode <= kMaxFrameDurationMs) {
            durationMs = *nextTimecode - frame.timecode;
            measured = true;
        }
        if (options.dropTransitionalFrames && measured &&
            durationMs <= options.transitionalLimitMs && previous && next &&
            isTransitionalFrame(*previous, frame.pixels, next->pixels)) {
            ++result.counts.transitional;
            return;
        }
        if (options.dropShortFrames && measured && durationMs < options.shortFrameLimitMs) {
            ++result.counts.tooShort;
            return;
        }
        if (options.dropColorfulFrames &&
            shadeCount(frame.usedShades) > static_cast<int>(options.colorLimit)) {
            ++result.counts.tooManyColors;
            return;
        }
        const uint32_t hash = legacy::legacyFrameHash(frame.pixels, nullptr, false);
        const auto earlier = seen.find(hash);
        if (earlier != seen.end()) {
            ++result.counts.duplicates;
            if (earlier->second != kNotKept &&
                result.frames[earlier->second].original.pixels != frame.pixels.pixels) {
                ++result.counts.hashCollisions;
            }
            return;
        }
        if (matcher) {
            bool collision = false;
            const ProjectFrameMatcher::Match match = matcher->match(frame.pixels, hash, &collision);
            if (match != ProjectFrameMatcher::Match::None) {
                seen.emplace(hash, kNotKept);
                if (match == ProjectFrameMatcher::Match::Identical) {
                    ++result.counts.identicalInProject;
                    result.counts.hashCollisions += collision ? 1 : 0;
                } else {
                    ++result.counts.identifiedByMaskOrShape;
                }
                return;
            }
        }
        seen.emplace(hash, result.frames.size());
        result.frames.push_back(ImportedFrame{std::move(frame.pixels), durationMs});
        ++result.counts.framesKept;
    };

    DumpFrame pending;
    DumpFrame current;
    bool hasPending = false;
    // The frame read before pending (take() moves pending's pixels away): for the filter of
    // transitional frames.
    document::IndexedFrame before;
    bool hasBefore = false;
    for (;;) {
        const FrameDumpReader::Status status = reader.next(current);
        if (status == FrameDumpReader::Status::Error) {
            return failed(std::move(result), reader.error().message, reader.error().line);
        }
        if (status == FrameDumpReader::Status::End) {
            break;
        }
        if (result.counts.framesRead == 0) {
            result.dumpFrameSize = reader.dumpFrameSize();
            result.storedFrameSize = reader.storedFrameSize();
            if (project) {
                if (project->originalFrameSize() != result.storedFrameSize) {
                    return failed(std::move(result),
                                  "The dump has " + sizeText(result.dumpFrameSize) +
                                      " frames" +
                                      (reader.isPadded() ? " (stored as " +
                                                               sizeText(result.storedFrameSize) +
                                                               ")"
                                                         : std::string()) +
                                      ", the original frames of the project are " +
                                      sizeText(project->originalFrameSize()) +
                                      ". Frames of another size cannot be added.");
                }
                matcher.emplace(*project, options.useMasksAndShapeModes);
            }
        }
        ++result.counts.framesRead;
        usedShades = static_cast<uint16_t>(usedShades | current.usedShades);
        if (hasPending) {
            document::IndexedFrame pendingPixels;
            if (options.dropTransitionalFrames) {
                pendingPixels = pending.pixels;
            }
            take(pending, hasBefore ? &before : nullptr, &current);
            if (options.dropTransitionalFrames) {
                before = std::move(pendingPixels);
                hasBefore = true;
            }
        }
        std::swap(pending, current);
        hasPending = true;
        if (progress && result.counts.framesRead % kProgressFrameInterval == 0 &&
            !progress(reader.bytesRead())) {
            result = failed(std::move(result), "cancelled");
            result.cancelled = true;
            return result;
        }
    }
    result.truncatedFrameLine = reader.truncatedFrameLine();
    if (!hasPending) {
        return failed(std::move(result),
                      result.truncatedFrameLine
                          ? "The dump ends inside its first frame; it holds no complete frame."
                          : "The file holds no frame.",
                      result.truncatedFrameLine.value_or(0));
    }
    take(pending, hasBefore ? &before : nullptr, nullptr);

    result.colorCount = (usedShades & 0xfff0u) != 0 ? 16 : 4;
    if (project && result.colorCount > project->colorCount()) {
        return failed(std::move(result),
                      "The dump has frames with shades above 3 and the project has " +
                          std::to_string(project->colorCount()) +
                          " original colors. A dump with 16 shades cannot be added to it.");
    }
    if (progress) {
        (void)progress(reader.bytesRead());
    }
    result.ok = true;
    return result;
}

DumpImportResult importFrameDumpFile(const std::string& path,
                                     const DumpImportOptions& options,
                                     const document::ProjectDocument* project,
                                     const DumpProgress& progress,
                                     uint64_t* totalBytes)
{
    // A .gz or .zip dump is decompressed while it is read.
    DumpFile file = openDumpFile(path);
    if (!file.ok()) {
        return failed({}, "The file cannot be read (" + file.error + "): " + path);
    }
    if (totalBytes) {
        *totalBytes = file.sizeBytes;
    }
    return importFrameDump(*file.stream, options, project, progress);
}

std::optional<render::ProjectGeometry> geometryForDump(render::Size storedSize)
{
    for (render::PlaneLayout layout : {render::PlaneLayout::Ordinary,
                                       render::PlaneLayout::NativeExtra,
                                       render::PlaneLayout::NativeOriginal}) {
        const render::ProjectGeometry geometry(layout);
        if (geometry.romFrameSize() == storedSize) {
            return geometry;
        }
    }
    return std::nullopt;
}

std::string romNameFromDumpPath(const std::string& path)
{
    const std::size_t separator = path.find_last_of("/\\");
    const std::string fileName = separator == std::string::npos ? path : path.substr(separator + 1);
    return fileName.substr(0, fileName.find('.')).substr(0, kMaxRomNameLength);
}

std::string romNameProblem(const std::string& name)
{
    if (name.empty()) {
        return "A ROM name is needed";
    }
    if (name.size() > kMaxRomNameLength) {
        return "A ROM name has at most " + std::to_string(kMaxRomNameLength) + " characters";
    }
    for (char character : name) {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x20 || value > 0x7e) {
            return "A ROM name has only unaccented letters, digits and signs";
        }
        if (std::string("/\\:*?\"<>|").find(character) != std::string::npos) {
            return std::string("A ROM name cannot contain ") + character;
        }
    }
    if (name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return "A ROM name cannot begin or end with a blank or end with a dot";
    }
    return {};
}

document::ProjectSnapshot makeProjectSnapshot(const std::string& romName, DumpImportResult&& dump)
{
    const std::string problem = romNameProblem(romName);
    if (!problem.empty()) {
        throw std::invalid_argument(problem);
    }
    const std::optional<render::ProjectGeometry> geometry = geometryForDump(dump.storedFrameSize);
    if (!geometry) {
        throw std::invalid_argument("no project layout has " + sizeText(dump.storedFrameSize) +
                                    " original frames");
    }
    if (!dump.ok || dump.frames.empty()) {
        throw std::invalid_argument("the dump holds no frame for a project");
    }

    document::ProjectSnapshot snapshot;
    snapshot.romName = romName;
    snapshot.colorCount = dump.colorCount;
    snapshot.geometry = *geometry;
    snapshot.frames.reserve(dump.frames.size());
    for (ImportedFrame& frame : dump.frames) {
        snapshot.frames.push_back(document::makeUncolorizedFrameSnapshot(
            *geometry, dump.colorCount, std::move(frame.original), frame.durationMs));
    }
    dump.frames.clear();

    // What Load_TXT_File and InitVariables set in a cRP that is otherwise zero: the plain
    // original colors at the start of palette 0 and as the edit colors.
    const std::vector<uint16_t> originalColors = legacy::legacyOriginalColors(dump.colorCount);
    document::PaletteRecord emptyPalette;
    emptyPalette.colors.assign(document::kPaletteColorCount, 0);
    snapshot.palettes.assign(document::kPaletteCount, emptyPalette);
    for (std::size_t color = 0; color < originalColors.size(); ++color) {
        snapshot.palettes[0].colors[color] = originalColors[color];
        snapshot.editorMetadata.editColors[color] = originalColors[color];
    }
    return snapshot;
}

std::vector<document::FrameRecord> makeProjectFrames(const document::ProjectDocument& project,
                                                     DumpImportResult&& dump)
{
    std::vector<document::FrameRecord> frames;
    frames.reserve(dump.frames.size());
    for (ImportedFrame& frame : dump.frames) {
        frames.push_back(project.makeUncolorizedFrame(std::move(frame.original), frame.durationMs));
    }
    dump.frames.clear();
    return frames;
}

}  // namespace colorizing::importing

#include "edit/FrameSettingsCopy.h"

#include <algorithm>
#include <stdexcept>

#include "edit/EditCommand.h"
#include "edit/FrameContentCommand.h"
#include "edit/SpeakerLightEdits.h"
#include "edit/StateCommands.h"

namespace colorizing::edit {

namespace {

using document::FrameDynamicData;
using document::ProjectDocument;
using render::Resolution;

// The planes a frame's pictures are edited on: both in an ordinary project, the native one in
// the others.
std::vector<Resolution> picturePlanes(const ProjectDocument& document)
{
    if (document.editsOriginalPlaneOnly()) {
        return {Resolution::Sd};
    }
    if (document.rendersHdPlaneOnly()) {
        return {Resolution::Hd};
    }
    return {Resolution::Sd, Resolution::Hd};
}

// HD data goes only between frames that both have HD.
bool planeOfBoth(const ProjectDocument& document,
                 std::size_t source,
                 std::size_t target,
                 Resolution plane)
{
    return plane == Resolution::Sd ||
           (document.hasHdFrame(source) && document.hasHdFrame(target));
}

const document::IndexedFrame& dynamicMask(const FrameDynamicData& data, Resolution plane)
{
    return plane == Resolution::Hd ? data.maskHd : data.maskSd;
}

const document::CompactArray<uint16_t>& dynamicColors(const FrameDynamicData& data,
                                                      Resolution plane)
{
    return plane == Resolution::Hd ? data.colorsHd : data.colorsSd;
}

const document::CompactArray<uint8_t>& shadowDirections(const FrameDynamicData& data,
                                                        Resolution plane)
{
    return plane == Resolution::Hd ? data.shadowDirectionsHd : data.shadowDirectionsSd;
}

const document::CompactArray<uint16_t>& shadowColors(const FrameDynamicData& data,
                                                     Resolution plane)
{
    return plane == Resolution::Hd ? data.shadowColorsHd : data.shadowColorsSd;
}

const document::IndexedFrame& backgroundMask(const document::FrameBackgroundData& data,
                                             Resolution plane)
{
    return plane == Resolution::Hd ? data.maskHd : data.maskSd;
}

// The used sprite slots, up to the first empty one.
std::vector<document::FrameSpriteReference> usedSlots(const ProjectDocument& document,
                                                      std::size_t frame)
{
    std::vector<document::FrameSpriteReference> used;
    for (const document::FrameSpriteReference& reference : document.frameSpriteReferences(frame)) {
        if (reference.spriteId == document::kNoSpriteId) {
            break;
        }
        used.push_back(reference);
    }
    return used;
}

bool sameSlots(const std::vector<document::FrameSpriteReference>& a,
               const std::vector<document::FrameSpriteReference>& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const document::FrameSpriteReference& x,
                         const document::FrameSpriteReference& y) {
                          return x.spriteId == y.spriteId && x.minX == y.minX &&
                                 x.minY == y.minY && x.maxX == y.maxX && x.maxY == y.maxY;
                      });
}

// Whether the part would change the target; false for a target that cannot take it.
bool changes(const ProjectDocument& document,
             std::size_t source,
             std::size_t target,
             FrameSettingsPart part)
{
    if (source == target ||
        (document.sceneFrame(target) && !sceneFrameTakes(part))) {
        return false;
    }
    const Resolution dynamicPlane = document.dynamicPlane();
    const auto& sourceDynamic = document.dynamicData(source);
    const auto& targetDynamic = document.dynamicData(target);
    const auto& sourceBackground = document.backgroundData(source);
    const auto& targetBackground = document.backgroundData(target);
    switch (part) {
        case FrameSettingsPart::DynamicZones:
            return sourceDynamic && targetDynamic &&
                   !(dynamicMask(*sourceDynamic, dynamicPlane).pixels ==
                     dynamicMask(*targetDynamic, dynamicPlane).pixels);
        case FrameSettingsPart::DynamicColors:
            return sourceDynamic && targetDynamic &&
                   (!(dynamicColors(*sourceDynamic, dynamicPlane) ==
                      dynamicColors(*targetDynamic, dynamicPlane)) ||
                    document.originalOnlyDynamicSets(source) !=
                        document.originalOnlyDynamicSets(target));
        case FrameSettingsPart::DynamicShadows:
            return sourceDynamic && targetDynamic &&
                   (!(shadowDirections(*sourceDynamic, dynamicPlane) ==
                      shadowDirections(*targetDynamic, dynamicPlane)) ||
                    !(shadowColors(*sourceDynamic, dynamicPlane) ==
                      shadowColors(*targetDynamic, dynamicPlane)));
        case FrameSettingsPart::Background:
            return sourceBackground && targetBackground &&
                   sourceBackground->backgroundId != targetBackground->backgroundId;
        case FrameSettingsPart::BackgroundMask:
            if (!sourceBackground || !targetBackground) {
                return false;
            }
            for (Resolution plane : picturePlanes(document)) {
                if (planeOfBoth(document, source, target, plane) &&
                    !(backgroundMask(*sourceBackground, plane).pixels ==
                      backgroundMask(*targetBackground, plane).pixels)) {
                    return true;
                }
            }
            return false;
        case FrameSettingsPart::Sprites:
            return !sameSlots(usedSlots(document, source), usedSlots(document, target));
        case FrameSettingsPart::ColorRotations:
            for (Resolution plane : picturePlanes(document)) {
                if (planeOfBoth(document, source, target, plane) &&
                    !(document.colorRotations(source, plane) ==
                      document.colorRotations(target, plane))) {
                    return true;
                }
            }
            return false;
        case FrameSettingsPart::Identification:
            return document.comparisonMaskId(source) != document.comparisonMaskId(target) ||
                   document.shapeComparisonMode(source) != document.shapeComparisonMode(target);
        case FrameSettingsPart::Trigger:
            return document.triggerId(source) != document.triggerId(target);
        case FrameSettingsPart::Lights:
            return document.frameLights(source) != document.frameLights(target);
        case FrameSettingsPart::DofEvents:
            return document.frameDofEvents(source) != document.frameDofEvents(target);
        case FrameSettingsPart::Duration:
            return document.frameDurationMs(source) != document.frameDurationMs(target);
    }
    return false;
}

void checkFrame(const ProjectDocument& document, std::size_t frame)
{
    if (frame >= document.frameCount()) {
        throw std::out_of_range("frame index out of range");
    }
}

std::unique_ptr<EditCommand> applied(std::unique_ptr<EditCommand> command,
                                     ProjectDocument& document)
{
    if (command) {
        command->apply(document);
    }
    return command;
}

std::unique_ptr<EditCommand> attribute(ProjectDocument& document,
                                       std::size_t target,
                                       FrameAttribute which,
                                       uint32_t value)
{
    const uint32_t before = frameAttributeValue(document, target, which);
    if (before == value) {
        return nullptr;
    }
    return applied(std::make_unique<FrameAttributeEditCommand>(target, which, before, value),
                   document);
}

// The dynamic parts of one target in one command, on the dynamic plane.
std::unique_ptr<EditCommand> copyDynamic(ProjectDocument& document,
                                         std::size_t source,
                                         std::size_t target,
                                         bool zones,
                                         bool colors,
                                         bool shadows,
                                         std::vector<std::unique_ptr<EditCommand>>& extra)
{
    const Resolution plane = document.dynamicPlane();
    const FrameDynamicData& from = *document.dynamicData(source);
    FrameContentBuilder builder(document, target, plane);
    if (zones) {
        const document::IndexedFrame& mask = dynamicMask(from, plane);
        for (int y = 0; y < mask.size.height; ++y) {
            for (int x = 0; x < mask.size.width; ++x) {
                builder.setDynamicMask(
                    render::Point{x, y},
                    mask.pixels[static_cast<std::size_t>(y) * mask.size.width + x]);
            }
        }
    }
    if (colors) {
        const document::CompactArray<uint16_t>& values = dynamicColors(from, plane);
        const std::size_t perSet = values.size() / document::kMaxDynamicSetsPerFrame;
        for (std::size_t set = 0; set < document::kMaxDynamicSetsPerFrame; ++set) {
            for (std::size_t color = 0; color < perSet; ++color) {
                builder.setDynamicColor(set, color, values[set * perSet + color]);
            }
        }
        const uint32_t before = document.originalOnlyDynamicSets(target);
        const uint32_t after = document.originalOnlyDynamicSets(source);
        if (before != after) {
            extra.push_back(applied(
                std::make_unique<OriginalOnlyDynamicSetsCommand>(target, before, after),
                document));
        }
    }
    if (shadows) {
        const document::CompactArray<uint8_t>& directions = shadowDirections(from, plane);
        const document::CompactArray<uint16_t>& shadowColor = shadowColors(from, plane);
        for (std::size_t set = 0;
             set < document::kMaxDynamicSetsPerFrame && set < directions.size(); ++set) {
            builder.setDynamicShadow(set, directions[set],
                                     set < shadowColor.size() ? shadowColor[set] : uint16_t{0});
        }
    }
    FrameContentChanges content = builder.changes();
    if (content.isEmpty()) {
        return nullptr;
    }
    return applied(std::make_unique<FrameContentCommand>(target, plane, std::move(content),
                                                         EditKind::DynamicMask),
                   document);
}

std::unique_ptr<EditCommand> copyBackgroundMasks(ProjectDocument& document,
                                                 std::size_t source,
                                                 std::size_t target)
{
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (Resolution plane : picturePlanes(document)) {
        if (!planeOfBoth(document, source, target, plane)) {
            continue;
        }
        const document::IndexedFrame& from =
            backgroundMask(*document.backgroundData(source), plane);
        const document::IndexedFrame& to =
            backgroundMask(*document.backgroundData(target), plane);
        std::vector<MaskPixelChange> maskChanges;
        for (int y = 0; y < from.size.height; ++y) {
            for (int x = 0; x < from.size.width; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * from.size.width + x;
                if (from.pixels[index] != to.pixels[index]) {
                    maskChanges.push_back({x, y, to.pixels[index], from.pixels[index]});
                }
            }
        }
        if (!maskChanges.empty()) {
            commands.push_back(applied(std::make_unique<MaskEditCommand>(
                                           EditKind::BackgroundMask, target, plane,
                                           std::move(maskChanges)),
                                       document));
        }
    }
    return groupCommands(std::move(commands));
}

std::unique_ptr<EditCommand> copySprites(ProjectDocument& document,
                                         std::size_t source,
                                         std::size_t target)
{
    const std::vector<document::FrameSpriteReference> before =
        document.frameSpriteReferences(target);
    const std::vector<document::FrameSpriteReference> wanted = usedSlots(document, source);
    // Emptied from the last slot, then filled from the first: no used slot may follow an
    // empty one.
    const std::vector<document::FrameSpriteReference> current = usedSlots(document, target);
    for (std::size_t slot = current.size(); slot-- > 0;) {
        document.clearFrameSpriteReference(target, slot);
    }
    for (std::size_t slot = 0; slot < wanted.size(); ++slot) {
        document.setFrameSpriteReference(target, slot, wanted[slot]);
    }
    return frameSpriteSlotsCommand(document, target, before);
}

std::unique_ptr<EditCommand> copyRotations(ProjectDocument& document,
                                           std::size_t source,
                                           std::size_t target)
{
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (Resolution plane : picturePlanes(document)) {
        if (!planeOfBoth(document, source, target, plane)) {
            continue;
        }
        const document::CompactArray<uint16_t> before = document.colorRotations(target, plane);
        const document::CompactArray<uint16_t> after = document.colorRotations(source, plane);
        if (!(before == after)) {
            commands.push_back(applied(
                std::make_unique<ColorRotationsCommand>(target, plane, before, after), document));
        }
    }
    return groupCommands(std::move(commands));
}

}  // namespace

bool sceneFrameTakes(FrameSettingsPart part)
{
    switch (part) {
        case FrameSettingsPart::Background:
        case FrameSettingsPart::BackgroundMask:
        case FrameSettingsPart::ColorRotations:
        case FrameSettingsPart::Lights:
        case FrameSettingsPart::DofEvents:
        case FrameSettingsPart::Duration:
            return true;
        default:
            return false;
    }
}

std::size_t framesChangedBy(const ProjectDocument& document,
                            std::size_t source,
                            const std::vector<std::size_t>& targets,
                            FrameSettingsPart part)
{
    checkFrame(document, source);
    std::size_t changed = 0;
    for (std::size_t target : targets) {
        checkFrame(document, target);
        if (changes(document, source, target, part)) {
            ++changed;
        }
    }
    return changed;
}

FrameSettingsCopy copyFrameSettings(ProjectDocument& document,
                                    std::size_t source,
                                    const std::vector<std::size_t>& targets,
                                    const std::vector<FrameSettingsPart>& parts)
{
    checkFrame(document, source);
    for (std::size_t target : targets) {
        checkFrame(document, target);
    }
    const auto chosen = [&parts](FrameSettingsPart part) {
        return std::find(parts.begin(), parts.end(), part) != parts.end();
    };
    FrameSettingsCopy result;
    EditBatch batch("Copy Frame Settings");
    try {
        for (std::size_t target : targets) {
            if (target == source) {
                continue;
            }
            bool leftOut = false;
            bool changed = false;
            const auto wants = [&](FrameSettingsPart part) {
                if (!chosen(part)) {
                    return false;
                }
                if (document.sceneFrame(target) && !sceneFrameTakes(part)) {
                    leftOut = true;
                    return false;
                }
                if (!changes(document, source, target, part)) {
                    return false;
                }
                changed = true;
                return true;
            };
            const bool zones = wants(FrameSettingsPart::DynamicZones);
            const bool colors = wants(FrameSettingsPart::DynamicColors);
            const bool shadows = wants(FrameSettingsPart::DynamicShadows);
            if (zones || colors || shadows) {
                std::vector<std::unique_ptr<EditCommand>> extra;
                batch.add(copyDynamic(document, source, target, zones, colors, shadows, extra));
                for (std::unique_ptr<EditCommand>& command : extra) {
                    batch.add(std::move(command));
                }
            }
            if (wants(FrameSettingsPart::Background)) {
                batch.add(attribute(document, target, FrameAttribute::BackgroundId,
                                    document.backgroundId(source)));
            }
            if (wants(FrameSettingsPart::BackgroundMask)) {
                batch.add(copyBackgroundMasks(document, source, target));
            }
            if (wants(FrameSettingsPart::Sprites)) {
                batch.add(copySprites(document, source, target));
            }
            if (wants(FrameSettingsPart::ColorRotations)) {
                batch.add(copyRotations(document, source, target));
            }
            if (wants(FrameSettingsPart::Identification)) {
                batch.add(attribute(document, target, FrameAttribute::ComparisonMaskId,
                                    document.comparisonMaskId(source)));
                batch.add(attribute(document, target, FrameAttribute::ShapeComparisonMode,
                                    document.shapeComparisonMode(source)));
            }
            if (wants(FrameSettingsPart::Trigger)) {
                batch.add(attribute(document, target, FrameAttribute::TriggerId,
                                    document.triggerId(source)));
            }
            if (wants(FrameSettingsPart::Lights)) {
                batch.add(applied(std::make_unique<FrameLightsCommand>(
                                      target, document.frameLights(target),
                                      document.frameLights(source)),
                                  document));
            }
            if (wants(FrameSettingsPart::DofEvents)) {
                batch.add(applied(std::make_unique<FrameDofEventsCommand>(
                                      target, document.frameDofEvents(target),
                                      document.frameDofEvents(source)),
                                  document));
            }
            if (wants(FrameSettingsPart::Duration)) {
                batch.add(attribute(document, target, FrameAttribute::DurationMs,
                                    document.frameDurationMs(source)));
            }
            result.changedFrames += changed ? 1 : 0;
            result.sceneFramesLeftOut += leftOut ? 1 : 0;
        }
    } catch (...) {
        batch.cancel(document);
        throw;
    }
    result.command = batch.finish();
    return result;
}

}  // namespace colorizing::edit

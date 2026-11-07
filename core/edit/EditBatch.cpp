#include "edit/EditBatch.h"

#include <set>
#include <stdexcept>
#include <utility>

#include "document/ProjectDocument.h"
#include "edit/FrameContentCommand.h"
#include "edit/FrameSettingsCopy.h"
#include "edit/SameFrames.h"
#include "edit/StateCommands.h"
#include "edit/StructureCommand.h"
#include "edit/UndoHistories.h"

namespace colorizing::edit {

namespace {

using document::ProjectDocument;
using render::Resolution;

const EditTarget kStructureTarget{EditKind::ProjectStructure, 0, Resolution::Sd, {}};

void checkFrame(const ProjectDocument& document, std::size_t frameIndex)
{
    if (frameIndex >= document.frameCount()) {
        throw std::out_of_range("frame index out of range");
    }
}

// The planes the frame has a colorized picture on.
std::vector<Resolution> colorizedPlanes(const ProjectDocument& document, std::size_t frameIndex)
{
    std::vector<Resolution> planes;
    for (Resolution plane : {Resolution::Sd, Resolution::Hd}) {
        if (!document.geometry().isEditable(plane)) {
            continue;
        }
        try {
            if (document.colorizedFrame(frameIndex, plane).isValid()) {
                planes.push_back(plane);
            }
        } catch (const std::exception&) {
            // No picture on this plane (an SD frame without HD).
        }
    }
    return planes;
}

std::unique_ptr<EditCommand> applied(std::unique_ptr<EditCommand> command,
                                     ProjectDocument& document)
{
    if (command) {
        command->apply(document);
    }
    return command;
}

// Throws unless the plane can be edited and the picture and the write mask have its size.
void checkPicture(const ProjectDocument& document,
                  Resolution plane,
                  render::Size size,
                  const std::vector<uint16_t>& pixels,
                  const std::vector<uint8_t>& write)
{
    if (!document.geometry().isEditable(plane)) {
        throw std::logic_error("the project does not edit this plane");
    }
    const std::size_t count =
        static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height);
    if (pixels.size() != count || (!write.empty() && write.size() != count)) {
        throw std::invalid_argument("the picture has not the size of the plane");
    }
}

// The changes that turn the current picture into the new one where the mask writes.
template <typename Read>
std::vector<PixelChange> pictureChanges(render::Size size,
                                        const std::vector<uint16_t>& pixels,
                                        const std::vector<uint8_t>& write,
                                        Read&& current)
{
    std::vector<PixelChange> changes;
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(size.width) +
                static_cast<std::size_t>(x);
            if (!write.empty() && write[index] == 0) {
                continue;
            }
            const uint16_t old = current(x, y);
            if (old != pixels[index]) {
                changes.push_back(PixelChange{x, y, old, pixels[index]});
            }
        }
    }
    return changes;
}

// Applies the pixel changes as the step after `created` (the HD the picture needed, or
// nullptr) and returns both as one command; takes `created` back when the changes fail.
std::unique_ptr<EditCommand> withPixels(ProjectDocument& document,
                                        std::unique_ptr<EditCommand> created,
                                        EditKind kind,
                                        std::size_t index,
                                        Resolution plane,
                                        std::vector<PixelChange> changes)
{
    std::vector<std::unique_ptr<EditCommand>> commands;
    if (created) {
        commands.push_back(std::move(created));
    }
    if (!changes.empty()) {
        try {
            commands.push_back(applied(
                std::make_unique<PixelEditCommand>(kind, index, plane, std::move(changes)),
                document));
        } catch (...) {
            if (!commands.empty()) {
                commands.front()->revert(document);
            }
            throw;
        }
    }
    return groupCommands(std::move(commands));
}

}  // namespace

EditBatchCommand::EditBatchCommand(std::string name,
                                   std::vector<std::unique_ptr<EditCommand>> steps)
    : m_name(std::move(name))
    , m_steps(std::move(steps))
{
    if (m_steps.empty()) {
        throw std::invalid_argument("an edit batch needs a step");
    }
    for (const std::unique_ptr<EditCommand>& step : m_steps) {
        if (!step) {
            throw std::invalid_argument("an edit batch needs a step");
        }
    }
}

void EditBatchCommand::apply(ProjectDocument& document) const
{
    std::size_t applied = 0;
    try {
        for (; applied < m_steps.size(); ++applied) {
            m_steps[applied]->apply(document);
        }
    } catch (...) {
        // Each step leaves the document as it was when it throws; take back the others.
        while (applied > 0) {
            m_steps[--applied]->revert(document);
        }
        throw;
    }
}

void EditBatchCommand::revert(ProjectDocument& document) const
{
    std::size_t remaining = m_steps.size();
    try {
        for (; remaining > 0; --remaining) {
            m_steps[remaining - 1]->revert(document);
        }
    } catch (...) {
        for (; remaining < m_steps.size(); ++remaining) {
            m_steps[remaining]->apply(document);
        }
        throw;
    }
}

EditTarget EditBatchCommand::target() const
{
    return kStructureTarget;
}

std::vector<ReplacedData> EditBatchCommand::replacedData() const
{
    std::vector<ReplacedData> replaced;
    const HistoryKey structure = structureHistoryKey();
    for (const std::unique_ptr<EditCommand>& step : m_steps) {
        const std::vector<ReplacedData> more = step->replacedData();
        replaced.insert(replaced.end(), more.begin(), more.end());
        // The history the step would have had on its own, and for frame content also the one
        // of the dynamic zones it may change.
        const HistoryKey key = historyKey(step->target());
        if (!(key == structure)) {
            replaced.push_back(ReplacedData{key.kind, key.index, key.resolution});
        }
        if (const auto* content = dynamic_cast<const FrameContentCommand*>(step.get())) {
            for (Resolution plane : {Resolution::Sd, Resolution::Hd}) {
                replaced.push_back(ReplacedData{EditKind::FramePixels, content->frameIndex(), plane});
                replaced.push_back(ReplacedData{EditKind::DynamicMask, content->frameIndex(), plane});
            }
        }
    }
    return replaced;
}

std::vector<RecordReference> EditBatchCommand::recordReferences() const
{
    // As the structure commands: it is undone in strict order with them, see StructureCommand.
    return {};
}

std::vector<RenumberedRecords> EditBatchCommand::renumberedRecords() const
{
    std::vector<RenumberedRecords> renumbered;
    for (const std::unique_ptr<EditCommand>& step : m_steps) {
        const std::vector<RenumberedRecords> more = step->renumberedRecords();
        renumbered.insert(renumbered.end(), more.begin(), more.end());
    }
    return renumbered;
}

std::size_t EditBatchCommand::heldBytes() const
{
    std::size_t bytes = 0;
    for (const std::unique_ptr<EditCommand>& step : m_steps) {
        bytes += step->heldBytes();
    }
    return bytes;
}

const std::string& EditBatchCommand::name() const
{
    return m_name;
}

std::size_t EditBatchCommand::stepCount() const
{
    return m_steps.size();
}

bool EditBatchCommand::changesStructure() const
{
    return !renumberedRecords().empty();
}

EditBatch::EditBatch(std::string name)
    : m_name(std::move(name))
{
}

void EditBatch::add(std::unique_ptr<EditCommand> step)
{
    if (step) {
        m_steps.push_back(std::move(step));
    }
}

bool EditBatch::isEmpty() const
{
    return m_steps.empty();
}

std::unique_ptr<EditBatchCommand> EditBatch::finish()
{
    if (m_steps.empty()) {
        return nullptr;
    }
    return std::make_unique<EditBatchCommand>(std::move(m_name), std::move(m_steps));
}

void EditBatch::cancel(ProjectDocument& document)
{
    while (!m_steps.empty()) {
        m_steps.back()->revert(document);
        m_steps.pop_back();
    }
}

std::unique_ptr<EditCommand> resetPixels(ProjectDocument& document,
                                         std::size_t frameIndex,
                                         const std::vector<uint8_t>& keep,
                                         std::optional<uint16_t> fill)
{
    checkFrame(document, frameIndex);
    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    if (keep.size() != original.pixelCount()) {
        throw std::invalid_argument("the keep mask has not the size of the ROM frame");
    }
    const std::vector<uint16_t> colors = document::plainOriginalColors(document.colorCount());
    const int romWidth = original.size.width;
    const int romHeight = original.size.height;
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (Resolution plane : colorizedPlanes(document, frameIndex)) {
        const render::Size size = document.colorizedFrame(frameIndex, plane).size;
        FrameContentBuilder builder(document, frameIndex, plane);
        for (int y = 0; y < size.height; ++y) {
            const int romY = y * romHeight / size.height;
            for (int x = 0; x < size.width; ++x) {
                const int romX = x * romWidth / size.width;
                const std::size_t romIndex = static_cast<std::size_t>(romY) *
                                                 static_cast<std::size_t>(romWidth) +
                                             static_cast<std::size_t>(romX);
                if (keep[romIndex] != 0) {
                    continue;
                }
                const uint8_t shade = original.pixels[romIndex];
                builder.setPixel(render::Point{x, y},
                                 fill ? *fill
                                      : (shade < colors.size() ? colors[shade] : uint16_t{0}));
            }
        }
        FrameContentChanges changes = builder.changes();
        if (!changes.isEmpty()) {
            commands.push_back(applied(std::make_unique<FrameContentCommand>(
                                           frameIndex, document.dynamicPlane(), std::move(changes)),
                                       document));
        }
    }
    return groupCommands(std::move(commands));
}

std::unique_ptr<EditCommand> setDynamicZones(ProjectDocument& document,
                                             std::size_t frameIndex,
                                             const std::vector<uint8_t>& sets)
{
    checkFrame(document, frameIndex);
    const Resolution plane = document.dynamicPlane();
    const render::Size size = document.geometry().planeSize(plane);
    if (sets.size() != static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height)) {
        throw std::invalid_argument("the dynamic zones have not the size of the dynamic plane");
    }
    for (uint8_t set : sets) {
        if (set != 255 && set >= document::kMaxDynamicSetsPerFrame) {
            throw std::invalid_argument("a dynamic set out of range");
        }
    }
    if (!document.dynamicData(frameIndex)) {
        throw std::logic_error("the frame has no dynamic data");
    }
    FrameContentBuilder builder(document, frameIndex, plane);
    for (int y = 0; y < size.height; ++y) {
        for (int x = 0; x < size.width; ++x) {
            builder.setDynamicMask(render::Point{x, y},
                                   sets[static_cast<std::size_t>(y) *
                                            static_cast<std::size_t>(size.width) +
                                        static_cast<std::size_t>(x)]);
        }
    }
    FrameContentChanges changes = builder.changes();
    if (changes.isEmpty()) {
        return nullptr;
    }
    return applied(std::make_unique<FrameContentCommand>(frameIndex, plane, std::move(changes),
                                                         EditKind::DynamicMask),
                   document);
}

std::unique_ptr<EditCommand> setFrameBackground(ProjectDocument& document,
                                                std::size_t frameIndex,
                                                uint16_t backgroundId)
{
    checkFrame(document, frameIndex);
    if (backgroundId != document::kNoBackgroundId && backgroundId >= document.backgroundCount()) {
        throw std::out_of_range("background index out of range");
    }
    if (!document.backgroundData(frameIndex)) {
        throw std::logic_error("the frame has no background data");
    }
    const uint32_t before =
        frameAttributeValue(document, frameIndex, FrameAttribute::BackgroundId);
    if (before == backgroundId) {
        return nullptr;
    }
    return applied(std::make_unique<FrameAttributeEditCommand>(
                       frameIndex, FrameAttribute::BackgroundId, before, backgroundId),
                   document);
}

std::unique_ptr<EditCommand> clearColorRotations(ProjectDocument& document,
                                                 std::size_t frameIndex)
{
    checkFrame(document, frameIndex);
    std::vector<std::unique_ptr<EditCommand>> commands;
    for (Resolution plane : colorizedPlanes(document, frameIndex)) {
        document::CompactArray<uint16_t> before = document.colorRotations(frameIndex, plane);
        bool rotates = false;
        for (std::size_t slot = 0; slot < document::kMaxColorRotations; ++slot) {
            rotates = rotates || document.colorRotationLength(frameIndex, plane, slot) > 0;
        }
        if (!rotates) {
            continue;
        }
        for (std::size_t slot = 0; slot < document::kMaxColorRotations; ++slot) {
            document.clearColorRotationSlot(frameIndex, plane, slot);
        }
        commands.push_back(colorRotationsCommand(document, frameIndex, plane, std::move(before)));
    }
    return groupCommands(std::move(commands));
}

std::unique_ptr<EditCommand> setFrameComparison(ProjectDocument& document,
                                                std::size_t frameIndex,
                                                uint8_t maskId,
                                                bool shape)
{
    checkFrame(document, frameIndex);
    if (maskId != document::kNoComparisonMaskId && maskId >= document.comparisonMaskCount()) {
        throw std::out_of_range("comparison mask index out of range");
    }
    std::vector<std::unique_ptr<EditCommand>> commands;
    const uint32_t maskBefore =
        frameAttributeValue(document, frameIndex, FrameAttribute::ComparisonMaskId);
    if (maskBefore != maskId) {
        commands.push_back(applied(std::make_unique<FrameAttributeEditCommand>(
                                       frameIndex, FrameAttribute::ComparisonMaskId, maskBefore,
                                       maskId),
                                   document));
    }
    const uint32_t shapeBefore =
        frameAttributeValue(document, frameIndex, FrameAttribute::ShapeComparisonMode);
    const uint32_t shapeAfter = shape ? 1 : 0;
    if (shapeBefore != shapeAfter) {
        commands.push_back(applied(std::make_unique<FrameAttributeEditCommand>(
                                       frameIndex, FrameAttribute::ShapeComparisonMode,
                                       shapeBefore, shapeAfter),
                                   document));
    }
    return groupCommands(std::move(commands));
}

std::unique_ptr<EditCommand> setComparisonMask(ProjectDocument& document,
                                               std::size_t maskIndex,
                                               const std::vector<uint8_t>& ignored)
{
    if (maskIndex >= document.comparisonMaskCount()) {
        throw std::out_of_range("comparison mask index out of range");
    }
    document::IndexedFrame mask;
    mask.size = document.originalFrameSize();
    mask.pixels.assign(ignored.begin(), ignored.end());
    return applied(replaceComparisonMask(document, maskIndex, mask), document);
}

std::unique_ptr<EditCommand> setDynamicColors(ProjectDocument& document,
                                              std::size_t frameIndex,
                                              std::size_t setIndex,
                                              const std::vector<uint16_t>& colors)
{
    checkFrame(document, frameIndex);
    if (setIndex >= document::kMaxDynamicSetsPerFrame) {
        throw std::out_of_range("dynamic set index out of range");
    }
    if (colors.size() != document.colorCount()) {
        throw std::invalid_argument("a dynamic set has a color per shade of the original");
    }
    if (!document.dynamicData(frameIndex)) {
        throw std::logic_error("the frame has no dynamic data");
    }
    const Resolution plane = document.dynamicPlane();
    std::vector<DynamicColorChange> changes;
    for (std::size_t shade = 0; shade < colors.size(); ++shade) {
        const uint16_t before = document.dynamicColor(frameIndex, plane, setIndex, shade);
        if (before != colors[shade]) {
            changes.push_back({shade, before, colors[shade]});
        }
    }
    if (changes.empty()) {
        return nullptr;
    }
    return applied(std::make_unique<DynamicColorsEditCommand>(frameIndex, plane, setIndex,
                                                              std::move(changes)),
                   document);
}

std::unique_ptr<EditCommand> createFramesHd(ProjectDocument& document,
                                            const std::vector<std::size_t>& frames,
                                            document::UpscaleFilter filter,
                                            bool backgroundMask,
                                            bool backgroundImage)
{
    if (document.rendersHdPlaneOnly() || document.editsOriginalPlaneOnly()) {
        throw std::logic_error("Create HD needs a project with SD and HD planes");
    }
    for (const std::size_t frame : frames) {
        checkFrame(document, frame);
    }
    EditBatch batch("Create HD");
    try {
        // A background several of the frames use is upscaled once, by the first of them.
        std::set<uint16_t> upscaledBackgrounds;
        for (const std::size_t frame : frames) {
            if (document.hasHdFrame(frame)) {
                continue;
            }
            const std::optional<document::FrameBackgroundData>& background =
                document.backgroundData(frame);
            document::FrameHdParts parts;
            parts.filter = filter;
            parts.backgroundMask = backgroundMask && background.has_value();
            parts.backgroundImage = backgroundImage && background &&
                                    background->backgroundId < document.backgroundCount() &&
                                    upscaledBackgrounds.insert(background->backgroundId).second;
            batch.add(createFrameHd(document, frame, parts));
        }
    } catch (...) {
        batch.cancel(document);
        throw;
    }
    return batch.finish();
}

std::unique_ptr<EditCommand> addFrameLike(ProjectDocument& document,
                                          const std::vector<uint8_t>& original,
                                          std::size_t like,
                                          std::size_t* added)
{
    checkFrame(document, like);
    document::IndexedFrame frame;
    frame.size = document.originalFrameSize();
    frame.pixels.assign(original.begin(), original.end());
    std::vector<document::FrameRecord> records;
    records.push_back(document.makeUncolorizedFrame(std::move(frame),
                                                    document.frameDurationMs(like)));

    EditBatch batch("Add frame");
    try {
        std::unique_ptr<StructureCommand> adding = addFrames(document, std::move(records));
        const std::size_t index = adding->recordIndices().front();
        batch.add(std::move(adding));
        // A frame added before the signing notice moves the notice one back.
        const std::size_t source = like >= index ? like + 1 : like;
        // The pictures first: an HD picture gives the new frame HD, which the settings of
        // that plane need.
        for (const Resolution plane : colorizedPlanes(document, source)) {
            batch.add(setFramePicture(document, index, plane,
                                      document.colorizedFrame(source, plane).pixels.toVector(),
                                      {}));
        }
        std::vector<FrameSettingsPart> parts;
        for (const FrameSettingsPart part : kFrameSettingsParts) {
            if (part != FrameSettingsPart::Trigger && part != FrameSettingsPart::DofEvents) {
                parts.push_back(part);
            }
        }
        batch.add(copyFrameSettings(document, source, {index}, parts).command);
        if (added) {
            *added = index;
        }
    } catch (...) {
        batch.cancel(document);
        throw;
    }
    return batch.finish();
}

std::unique_ptr<EditCommand> setFramePicture(ProjectDocument& document,
                                             std::size_t frameIndex,
                                             Resolution plane,
                                             const std::vector<uint16_t>& pixels,
                                             const std::vector<uint8_t>& write)
{
    checkFrame(document, frameIndex);
    checkPicture(document, plane, document.geometry().planeSize(plane), pixels, write);
    std::unique_ptr<EditCommand> created;
    if (plane == Resolution::Hd && !document.hasHdFrame(frameIndex)) {
        document::FrameHdParts parts;
        parts.backgroundMask = document.backgroundData(frameIndex).has_value();
        created = createFrameHd(document, frameIndex, parts);
    }
    std::vector<PixelChange> changes;
    try {
        const document::Rgb565Frame& current = document.colorizedFrame(frameIndex, plane);
        changes = pictureChanges(current.size, pixels, write, [&](int x, int y) {
            return current.pixels[static_cast<std::size_t>(y) *
                                      static_cast<std::size_t>(current.size.width) +
                                  static_cast<std::size_t>(x)];
        });
    } catch (...) {
        if (created) {
            created->revert(document);
        }
        throw;
    }
    return withPixels(document, std::move(created), EditKind::FramePixels, frameIndex, plane,
                      std::move(changes));
}

std::unique_ptr<EditCommand> setBackgroundPicture(ProjectDocument& document,
                                                  std::size_t backgroundIndex,
                                                  Resolution plane,
                                                  const std::vector<uint16_t>& pixels,
                                                  const std::vector<uint8_t>& write)
{
    if (backgroundIndex >= document.backgroundCount()) {
        throw std::out_of_range("background index out of range");
    }
    const render::Size size = document.geometry().planeSize(plane);
    checkPicture(document, plane, size, pixels, write);
    if (plane == Resolution::Hd && !document.background(backgroundIndex).colorizedHd) {
        // A new HD image: the picture where it writes, black elsewhere, in one step.
        document::Rgb565Frame frame;
        frame.size = size;
        std::vector<uint16_t> values(pixels);
        for (std::size_t index = 0; index < values.size() && !write.empty(); ++index) {
            if (write[index] == 0) {
                values[index] = 0;
            }
        }
        frame.pixels = document::CompactArray<uint16_t>(std::move(values));
        return applied(std::make_unique<BackgroundHdCreateCommand>(backgroundIndex,
                                                                   std::move(frame)),
                       document);
    }
    const document::Rgb565Frame& current = document.backgroundFrame(backgroundIndex, plane);
    std::vector<PixelChange> changes = pictureChanges(size, pixels, write, [&](int x, int y) {
        return current.pixels[static_cast<std::size_t>(y) *
                                  static_cast<std::size_t>(current.size.width) +
                              static_cast<std::size_t>(x)];
    });
    return withPixels(document, nullptr, EditKind::BackgroundImage, backgroundIndex, plane,
                      std::move(changes));
}

std::unique_ptr<EditCommand> setSpritePicture(ProjectDocument& document,
                                              std::size_t spriteIndex,
                                              Resolution plane,
                                              const std::vector<uint16_t>& pixels,
                                              const std::vector<uint8_t>& write)
{
    if (spriteIndex >= document.spriteCount()) {
        throw std::out_of_range("sprite index out of range");
    }
    const render::Size size{static_cast<int>(document::kMaxSpriteWidth),
                            static_cast<int>(document::kMaxSpriteHeight)};
    checkPicture(document, plane, size, pixels, write);
    std::unique_ptr<EditCommand> created;
    if (plane == Resolution::Hd && !document.hasHdSprite(spriteIndex)) {
        created = createSpriteHd(document, spriteIndex);
    }
    std::vector<PixelChange> changes = pictureChanges(size, pixels, write, [&](int x, int y) {
        return document.spriteColorizedPixel(spriteIndex, plane, x, y);
    });
    return withPixels(document, std::move(created), EditKind::SpriteImage, spriteIndex, plane,
                      std::move(changes));
}

render::Size checkPictureEdit(const ProjectDocument& document, const PictureEdit& picture)
{
    render::Size size = document.geometry().planeSize(picture.plane);
    switch (picture.kind) {
    case PictureKind::Frame:
        checkFrame(document, picture.index);
        break;
    case PictureKind::Background:
        if (picture.index >= document.backgroundCount()) {
            throw std::out_of_range("background index out of range");
        }
        break;
    case PictureKind::Sprite:
        if (picture.index >= document.spriteCount()) {
            throw std::out_of_range("sprite index out of range");
        }
        size = render::Size{static_cast<int>(document::kMaxSpriteWidth),
                            static_cast<int>(document::kMaxSpriteHeight)};
        break;
    }
    checkPicture(document, picture.plane, size, picture.pixels, picture.write);
    return size;
}

std::unique_ptr<EditCommand> setPicture(ProjectDocument& document, const PictureEdit& picture)
{
    switch (picture.kind) {
    case PictureKind::Frame:
        return setFramePicture(document, picture.index, picture.plane, picture.pixels,
                               picture.write);
    case PictureKind::Background:
        return setBackgroundPicture(document, picture.index, picture.plane, picture.pixels,
                                    picture.write);
    case PictureKind::Sprite:
        return setSpritePicture(document, picture.index, picture.plane, picture.pixels,
                                picture.write);
    }
    return nullptr;
}

}  // namespace colorizing::edit

#include "edit/FrameContentCommand.h"

#include <stdexcept>
#include <utility>

namespace colorizing::edit {

bool FrameContentChanges::isEmpty() const
{
    return pixels.empty() && dynamicMask.empty() && dynamicSets.empty();
}

bool FrameContentChanges::changesDynamicData() const
{
    return !dynamicMask.empty() || !dynamicSets.empty();
}

namespace {

std::size_t pointIndex(render::Size size, render::Point point)
{
    if (point.x < 0 || point.y < 0 || point.x >= size.width || point.y >= size.height) {
        throw std::out_of_range("frame content edit: pixel outside the plane");
    }
    return static_cast<std::size_t>(point.y) * static_cast<std::size_t>(size.width) +
           static_cast<std::size_t>(point.x);
}

}  // namespace

FrameContentBuilder::FrameContentBuilder(const document::ProjectDocument& document,
                                         std::size_t frameIndex,
                                         render::Resolution plane)
    : m_document(document),
      m_frameIndex(frameIndex),
      m_plane(plane)
{
}

void FrameContentBuilder::setPixel(render::Point point, uint16_t color)
{
    const document::Rgb565Frame& frame = m_document.colorizedFrame(m_frameIndex, m_plane);
    if (m_pixels.empty()) {
        m_pixels.assign(frame.pixels.size(), -1);
    }
    m_pixels[pointIndex(frame.size, point)] = color;
}

void FrameContentBuilder::setDynamicMask(render::Point point, uint8_t value)
{
    const std::optional<document::FrameDynamicData>& data = m_document.dynamicData(m_frameIndex);
    if (!data) {
        throw std::logic_error("frame content edit: the frame has no dynamic data");
    }
    const document::IndexedFrame& mask =
        m_document.dynamicPlane() == render::Resolution::Hd ? data->maskHd : data->maskSd;
    if (m_dynamicMask.empty()) {
        m_dynamicMask.assign(mask.pixels.size(), -1);
    }
    m_dynamicMask[pointIndex(mask.size, point)] = value;
}

void FrameContentBuilder::setDynamicColor(std::size_t setIndex,
                                          std::size_t originalColorIndex,
                                          uint16_t color)
{
    const std::size_t colorCount = m_document.colorCount();
    if (setIndex >= document::kMaxDynamicSetsPerFrame || originalColorIndex >= colorCount) {
        throw std::out_of_range("frame content edit: dynamic color does not exist");
    }
    if (m_dynamicColors.empty()) {
        m_dynamicColors.assign(document::kMaxDynamicSetsPerFrame * colorCount, -1);
    }
    m_dynamicColors[setIndex * colorCount + originalColorIndex] = color;
}

void FrameContentBuilder::setDynamicShadow(std::size_t setIndex, uint8_t direction, uint16_t color)
{
    if (setIndex >= document::kMaxDynamicSetsPerFrame) {
        throw std::out_of_range("frame content edit: dynamic set does not exist");
    }
    if (m_shadowDirections.empty()) {
        m_shadowDirections.assign(document::kMaxDynamicSetsPerFrame, -1);
        m_shadowColors.assign(document::kMaxDynamicSetsPerFrame, -1);
    }
    m_shadowDirections[setIndex] = direction;
    m_shadowColors[setIndex] = color;
}

FrameContentChanges FrameContentBuilder::changes() const
{
    FrameContentChanges changes;
    changes.plane = m_plane;
    if (!m_pixels.empty()) {
        const document::Rgb565Frame& frame = m_document.colorizedFrame(m_frameIndex, m_plane);
        for (std::size_t i = 0; i < m_pixels.size(); ++i) {
            if (m_pixels[i] < 0 || frame.pixels[i] == static_cast<uint16_t>(m_pixels[i])) {
                continue;
            }
            const auto width = static_cast<std::size_t>(frame.size.width);
            changes.pixels.push_back(PixelChange{static_cast<int>(i % width),
                                                 static_cast<int>(i / width),
                                                 frame.pixels[i],
                                                 static_cast<uint16_t>(m_pixels[i])});
        }
    }
    const render::Resolution dynamicPlane = m_document.dynamicPlane();
    if (!m_dynamicMask.empty()) {
        const document::FrameDynamicData& data = *m_document.dynamicData(m_frameIndex);
        const document::IndexedFrame& mask =
            dynamicPlane == render::Resolution::Hd ? data.maskHd : data.maskSd;
        for (std::size_t i = 0; i < m_dynamicMask.size(); ++i) {
            if (m_dynamicMask[i] < 0 || mask.pixels[i] == static_cast<uint8_t>(m_dynamicMask[i])) {
                continue;
            }
            const int width = mask.size.width;
            changes.dynamicMask.push_back(
                MaskPixelChange{static_cast<int>(i % static_cast<std::size_t>(width)),
                                static_cast<int>(i / static_cast<std::size_t>(width)),
                                mask.pixels[i],
                                static_cast<uint8_t>(m_dynamicMask[i])});
        }
    }
    const std::size_t colorCount = m_document.colorCount();
    for (std::size_t setIndex = 0; setIndex < document::kMaxDynamicSetsPerFrame; ++setIndex) {
        DynamicSetChange set;
        set.setIndex = setIndex;
        if (!m_dynamicColors.empty()) {
            for (std::size_t color = 0; color < colorCount; ++color) {
                const int32_t value = m_dynamicColors[setIndex * colorCount + color];
                if (value < 0) {
                    continue;
                }
                const uint16_t old =
                    m_document.dynamicColor(m_frameIndex, dynamicPlane, setIndex, color);
                if (old != static_cast<uint16_t>(value)) {
                    set.colors.push_back(
                        DynamicColorChange{color, old, static_cast<uint16_t>(value)});
                }
            }
        }
        if (!m_shadowDirections.empty() && m_shadowDirections[setIndex] >= 0) {
            const uint8_t oldDirection =
                m_document.dynamicShadowDirection(m_frameIndex, dynamicPlane, setIndex);
            const uint16_t oldColor =
                m_document.dynamicShadowColor(m_frameIndex, dynamicPlane, setIndex);
            const auto newDirection = static_cast<uint8_t>(m_shadowDirections[setIndex]);
            const auto newColor = static_cast<uint16_t>(m_shadowColors[setIndex]);
            if (oldDirection != newDirection || oldColor != newColor) {
                set.shadow = true;
                set.shadowChange =
                    DynamicShadowChange{oldDirection, newDirection, oldColor, newColor};
            }
        }
        if (!set.colors.empty() || set.shadow) {
            changes.dynamicSets.push_back(std::move(set));
        }
    }
    return changes;
}

FrameContentCommand::FrameContentCommand(std::size_t frameIndex,
                                         render::Resolution dynamicPlane,
                                         FrameContentChanges changes,
                                         EditKind historyKind)
    : m_frameIndex(frameIndex),
      m_dynamicPlane(dynamicPlane),
      m_changes(std::move(changes)),
      m_historyKind(historyKind)
{
    if (historyKind != EditKind::FramePixels && historyKind != EditKind::DynamicMask) {
        throw std::invalid_argument("frame content belongs to the pixel or dynamic mask history");
    }
}

void FrameContentCommand::apply(document::ProjectDocument& document) const
{
    check(document);
    write(document, true);
}

void FrameContentCommand::revert(document::ProjectDocument& document) const
{
    check(document);
    write(document, false);
}

EditTarget FrameContentCommand::target() const
{
    const render::Resolution resolution =
        m_historyKind == EditKind::DynamicMask ? m_dynamicPlane : m_changes.plane;
    return EditTarget{m_historyKind, m_frameIndex, resolution, {m_frameIndex}};
}

std::vector<RecordReference> FrameContentCommand::recordReferences() const
{
    return {{RecordKind::Frame, m_frameIndex}};
}

std::size_t FrameContentCommand::frameIndex() const
{
    return m_frameIndex;
}

const FrameContentChanges& FrameContentCommand::changes() const
{
    return m_changes;
}

void FrameContentCommand::check(const document::ProjectDocument& document) const
{
    if (m_frameIndex >= document.frameCount()) {
        throw std::out_of_range("frame content edit: frame does not exist");
    }
    if (!m_changes.pixels.empty()) {
        const document::Rgb565Frame& frame = document.colorizedFrame(m_frameIndex,
                                                                     m_changes.plane);
        for (const PixelChange& change : m_changes.pixels) {
            if (change.x < 0 || change.y < 0 || change.x >= frame.size.width ||
                change.y >= frame.size.height) {
                throw std::out_of_range("frame content edit: pixel outside the frame");
            }
        }
    }
    if (m_changes.changesDynamicData()) {
        if (m_dynamicPlane != document.dynamicPlane() || !document.dynamicData(m_frameIndex)) {
            throw std::logic_error("frame content edit: the frame has no dynamic data there");
        }
    }
}

void FrameContentCommand::write(document::ProjectDocument& document, bool useNewValues) const
{
    for (const PixelChange& change : m_changes.pixels) {
        document.setColorizedPixel(m_frameIndex,
                                   m_changes.plane,
                                   change.x,
                                   change.y,
                                   useNewValues ? change.newColor : change.oldColor);
    }
    for (const MaskPixelChange& change : m_changes.dynamicMask) {
        document.setDynamicMaskPixel(m_frameIndex,
                                     m_dynamicPlane,
                                     change.x,
                                     change.y,
                                     useNewValues ? change.newValue : change.oldValue);
    }
    for (const DynamicSetChange& set : m_changes.dynamicSets) {
        for (const DynamicColorChange& color : set.colors) {
            document.setDynamicColor(m_frameIndex,
                                     m_dynamicPlane,
                                     set.setIndex,
                                     color.originalColorIndex,
                                     useNewValues ? color.newColor : color.oldColor);
        }
        if (set.shadow) {
            const DynamicShadowChange& shadow = set.shadowChange;
            document.setDynamicShadowDirection(m_frameIndex,
                                               m_dynamicPlane,
                                               set.setIndex,
                                               useNewValues ? shadow.newDirection
                                                            : shadow.oldDirection);
            document.setDynamicShadowColor(m_frameIndex,
                                           m_dynamicPlane,
                                           set.setIndex,
                                           useNewValues ? shadow.newColor : shadow.oldColor);
        }
    }
}

}  // namespace colorizing::edit

#include "edit/SpriteClipboard.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "edit/StateCommands.h"

namespace colorizing::edit {

namespace {

constexpr int kSpriteWidth = static_cast<int>(document::kMaxSpriteWidth);
constexpr int kSpriteHeight = static_cast<int>(document::kMaxSpriteHeight);
constexpr uint8_t kTransparent = 255;

struct SpritePlane {
    const document::IndexedFrame* shades = nullptr;
    const document::Rgb565Frame* colors = nullptr;
};

SpritePlane spritePlane(const document::ProjectDocument& document,
                        std::size_t spriteIndex,
                        render::Resolution plane)
{
    const document::SpriteRecord& sprite = document.sprite(spriteIndex);
    if (document.rendersHdPlaneOnly()) {
        if (plane != render::Resolution::Hd) {
            throw std::logic_error("the sprite has its picture on the native plane only");
        }
        // The original holds the shapes and the ROM shades, the HD colors the picture.
        return SpritePlane{&sprite.originalSd, &sprite.colorizedHd};
    }
    if (plane == render::Resolution::Hd) {
        if (document.editsOriginalPlaneOnly() || !sprite.hasHd) {
            throw std::logic_error("the sprite has no HD picture");
        }
        return SpritePlane{&sprite.maskHd, &sprite.colorizedHd};
    }
    return SpritePlane{&sprite.originalSd, &sprite.colorizedSd};
}

std::size_t spriteIndexOf(int x, int y)
{
    return static_cast<std::size_t>(y) * document::kMaxSpriteWidth + static_cast<std::size_t>(x);
}

}  // namespace

CopyBuffer copySprite(const document::ProjectDocument& document,
                      std::size_t spriteIndex,
                      render::Resolution plane)
{
    const SpritePlane source = spritePlane(document, spriteIndex, plane);
    int minX = kSpriteWidth;
    int minY = kSpriteHeight;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < kSpriteHeight; ++y) {
        for (int x = 0; x < kSpriteWidth; ++x) {
            if (source.shades->pixels[spriteIndexOf(x, y)] != kTransparent) {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (maxX < 0) {
        throw std::invalid_argument("the sprite has no pixel to copy");
    }
    CopyBuffer buffer;
    buffer.geometry = document.geometry();
    buffer.colorCount = document.colorCount();
    buffer.plane = plane;
    buffer.fromSprite = true;
    buffer.box = render::Rect{minX, minY, maxX - minX + 1, maxY - minY + 1};
    const std::size_t count =
        static_cast<std::size_t>(buffer.box.width) * static_cast<std::size_t>(buffer.box.height);
    buffer.mask.assign(count, 0);
    buffer.colors.assign(count, 0);
    buffer.originals.assign(count, 0);
    std::size_t i = 0;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x, ++i) {
            const uint8_t shade = source.shades->pixels[spriteIndexOf(x, y)];
            if (shade == kTransparent) {
                continue;
            }
            buffer.mask[i] = 1;
            buffer.colors[i] = source.colors->pixels[spriteIndexOf(x, y)];
            buffer.originals[i] = shade;
        }
    }
    return buffer;
}

std::optional<std::string> spritePasteRefusal(const document::ProjectDocument& document,
                                              std::size_t spriteIndex,
                                              const CopyBuffer& buffer,
                                              render::Resolution plane)
{
    try {
        (void)spritePlane(document, spriteIndex, plane);
    } catch (const std::exception& ex) {
        return std::string(ex.what());
    }
    if (buffer.geometry.layout() != document.geometry().layout()) {
        return std::string("the clipboard comes from a project of another frame size");
    }
    if (buffer.plane != plane) {
        return std::string("the clipboard holds pixels of the other plane");
    }
    if (buffer.box.width > kSpriteWidth || buffer.box.height > kSpriteHeight) {
        return std::string("the clipboard is larger than a sprite (256x64)");
    }
    if (buffer.selectedCount() == 0) {
        return std::string("the clipboard is empty");
    }
    return std::nullopt;
}

std::unique_ptr<EditCommand> pasteIntoSprite(document::ProjectDocument& document,
                                             std::size_t spriteIndex,
                                             const CopyBuffer& buffer,
                                             render::Resolution plane,
                                             bool mirrorHorizontally,
                                             bool mirrorVertically)
{
    if (const std::optional<std::string> refusal =
            spritePasteRefusal(document, spriteIndex, buffer, plane)) {
        throw std::invalid_argument(*refusal);
    }
    const render::Size spriteSize{kSpriteWidth, kSpriteHeight};
    document::IndexedFrame shades = document::makeIndexedFrame(spriteSize, kTransparent);
    document::Rgb565Frame colors = document::makeRgb565Frame(spriteSize, 0);
    const PastePlacement placement{0, 0, mirrorHorizontally, mirrorVertically};
    for (int y = 0; y < buffer.box.height; ++y) {
        for (int x = 0; x < buffer.box.width; ++x) {
            const std::size_t source = pasteSourceIndex(buffer, placement, x, y);
            if (!buffer.mask[source]) {
                continue;
            }
            shades.pixels[spriteIndexOf(x, y)] = buffer.originals[source];
            colors.pixels[spriteIndexOf(x, y)] = buffer.colors[source];
        }
    }
    SpriteState before = spriteState(document, spriteIndex);
    document.replaceSpritePlane(spriteIndex, plane, std::move(shades), std::move(colors));
    // As IDC_PASTE: the sprite comes from the frame the pixels were copied from.
    if (!buffer.fromSprite && buffer.sourceFrame < document.frameCount()) {
        document.setSpriteSourceFrame(spriteIndex, static_cast<uint32_t>(buffer.sourceFrame));
    }
    return spriteStateCommand(document, spriteIndex, std::move(before));
}

}  // namespace colorizing::edit

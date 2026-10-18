#include "edit/SourceFrame.h"

#include <vector>

namespace colorizing::edit {

std::optional<document::IndexedFrame> sourceFrameFromColors(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    std::string* problem)
{
    const auto refuse = [problem](std::string reason) -> std::optional<document::IndexedFrame> {
        if (problem) {
            *problem = std::move(reason);
        }
        return std::nullopt;
    };
    if (frameIndex >= document.frameCount()) {
        return refuse("no such frame");
    }
    const render::Resolution plane = document.geometry().nativePlane();
    if (plane == render::Resolution::Hd && !document.hasHdFrame(frameIndex)) {
        return refuse("the frame has no picture on the plane of the original frames");
    }
    const document::Rgb565Frame& picture = document.colorizedFrame(frameIndex, plane);
    if (picture.size != document.originalFrameSize()) {
        return refuse("the picture does not have the size of the original frames");
    }
    const std::vector<uint16_t> colors = document::plainOriginalColors(document.colorCount());
    document::IndexedFrame original = document::makeIndexedFrame(picture.size, 0);
    for (std::size_t index = 0; index < picture.pixels.size(); ++index) {
        bool found = false;
        // The first shade of the color, as the Win32 editor's isInOriginalColors().
        for (std::size_t shade = 0; shade < colors.size(); ++shade) {
            if (colors[shade] == picture.pixels[index]) {
                original.pixels[index] = static_cast<uint8_t>(shade);
                found = true;
                break;
            }
        }
        if (!found) {
            const int width = picture.size.width;
            return refuse("the pixel at " + std::to_string(static_cast<int>(index) % width) + "," +
                          std::to_string(static_cast<int>(index) / width) +
                          " is not one of the original colors");
        }
    }
    return original;
}

}  // namespace colorizing::edit

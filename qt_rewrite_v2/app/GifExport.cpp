#include "GifExport.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <set>

#include "FrameImageUtils.h"
#include "export/GifWriter.h"

namespace colorizing::app {

namespace {

// The shortest delay viewers show as it is, in 1/100 s.
constexpr int kMinimumDelayCs = 2;

std::vector<uint16_t> scaledPicture(const document::Rgb565Frame& frame, int scale)
{
    const int width = frame.size.width * scale;
    const int height = frame.size.height * scale;
    const int dot = scale >= 3 ? scale - 1 : scale;
    std::vector<uint16_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                                 0);
    for (int y = 0; y < frame.size.height; ++y) {
        for (int x = 0; x < frame.size.width; ++x) {
            const uint16_t color =
                frame.pixels[static_cast<std::size_t>(y) * frame.size.width + x];
            for (int dy = 0; dy < dot; ++dy) {
                uint16_t* row = pixels.data() +
                                static_cast<std::size_t>(y * scale + dy) * width +
                                static_cast<std::size_t>(x * scale);
                std::fill(row, row + dot, color);
            }
        }
    }
    return pixels;
}

uint16_t toRgb565(const QColor& color)
{
    return static_cast<uint16_t>(((color.red() >> 3) << 11) | ((color.green() >> 2) << 5) |
                                 (color.blue() >> 3));
}

// The original frame in the editor's shades, as an RGB565 picture.
document::Rgb565Frame originalPicture(const document::ProjectDocument& document,
                                      std::size_t frameIndex)
{
    const document::IndexedFrame& original = document.originalFrame(frameIndex);
    document::Rgb565Frame picture;
    picture.size = original.size;
    std::vector<uint16_t> pixels(original.pixels.size());
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        pixels[index] =
            toRgb565(originalIndexToColor(original.pixels[index], document.colorCount()));
    }
    picture.pixels = document::CompactArray<uint16_t>(std::move(pixels));
    return picture;
}

// Copies a picture of the given width into the canvas at x, y.
void place(std::vector<uint16_t>& canvas,
           int canvasWidth,
           const std::vector<uint16_t>& picture,
           int pictureWidth,
           int x,
           int y)
{
    if (pictureWidth <= 0) {
        return;
    }
    const int rows = static_cast<int>(picture.size()) / pictureWidth;
    for (int row = 0; row < rows; ++row) {
        std::copy_n(picture.data() + static_cast<std::size_t>(row) * pictureWidth, pictureWidth,
                    canvas.data() + static_cast<std::size_t>(y + row) * canvasWidth + x);
    }
}

}  // namespace

std::vector<uint32_t> colorRotationSteps(const document::ProjectDocument& document,
                                         std::size_t frameIndex,
                                         render::Resolution plane,
                                         uint32_t durationMs)
{
    std::set<uint32_t> steps{0};
    for (std::size_t slot = 0; slot < document::kMaxColorRotations; ++slot) {
        const uint16_t length = document.colorRotationLength(frameIndex, plane, slot);
        const uint16_t delay = document.colorRotationDelayMs(frameIndex, plane, slot);
        if (length == 0 || delay == 0) {
            continue;
        }
        for (uint32_t t = delay; t < durationMs; t += delay) {
            steps.insert(t);
        }
    }
    return std::vector<uint32_t>(steps.begin(), steps.end());
}

GifExportResult writeAnimatedGif(const document::ProjectDocument& document,
                                 const std::vector<std::size_t>& frameIndices,
                                 const GifExportOptions& options,
                                 std::ostream& stream,
                                 legacy::LegacyIoProgress* progress)
{
    GifExportResult result;
    if (frameIndices.empty()) {
        result.error = "no frames to export";
        return result;
    }
    const render::Size planeSize = document.geometry().planeSize(options.plane);
    for (std::size_t frameIndex : frameIndices) {
        if (frameIndex >= document.frameCount()) {
            result.error = "frame " + std::to_string(frameIndex) + " does not exist";
            return result;
        }
        if (options.plane == render::Resolution::Hd && !document.hasHdFrame(frameIndex)) {
            result.error = "frame " + std::to_string(frameIndex) + " has no picture on this plane";
            return result;
        }
    }
    if (progress) {
        progress->total = frameIndices.size();
        progress->done = 0;
        progress->setStage(legacy::LegacyIoStage::ExportingFrames);
    }
    try {
        const int scale = std::max(1, options.pixelScale);
        // With the original: it on top, the colorized picture below, the narrower centred.
        const render::Size originalSize =
            options.includeOriginal ? document.originalFrameSize() : render::Size{0, 0};
        const int width = std::max(planeSize.width, originalSize.width) * scale;
        const int originalTop = 0;
        const int colorizedTop =
            options.includeOriginal ? originalSize.height * scale + kGifOriginalGap : 0;
        const int height = colorizedTop + planeSize.height * scale;
        const int colorizedLeft = (width - planeSize.width * scale) / 2;
        const int originalLeft = (width - originalSize.width * scale) / 2;
        exporting::GifWriter writer(stream, width, height);
        // The picture waiting to be written: it shows until the next one starts.
        std::optional<std::vector<uint16_t>> pending;
        uint64_t writtenCs = 0;
        const auto flush = [&](uint64_t endMs, bool last) {
            if (!pending) {
                return;
            }
            const uint64_t endCs = (endMs + 5) / 10;
            int delay = static_cast<int>(endCs - writtenCs);
            if (delay < kMinimumDelayCs && !last) {
                // Shown too briefly: the next picture takes its time.
                pending.reset();
                return;
            }
            delay = std::max(delay, kMinimumDelayCs);
            writer.addFrame(*pending, delay);
            writtenCs += static_cast<uint64_t>(delay);
            ++result.pictures;
            pending.reset();
        };
        uint64_t timeMs = 0;
        for (std::size_t n = 0; n < frameIndices.size(); ++n) {
            if (progress && progress->cancel.load()) {
                result.cancelled = true;
                result.error = legacy::kLegacyIoCancelledMessage;
                return result;
            }
            const std::size_t frameIndex = frameIndices[n];
            const uint32_t duration = document.frameDurationMs(frameIndex);
            std::vector<uint8_t> notRotating;
            const document::Rgb565Frame composed = composeResolvedFrame(
                document, frameIndex, options.plane, options.includeBackground, &notRotating);
            const std::vector<uint32_t> steps =
                colorRotationSteps(document, frameIndex, options.plane, duration);
            std::vector<uint16_t> canvas(static_cast<std::size_t>(width) * height, 0);
            if (options.includeOriginal) {
                place(canvas, width, scaledPicture(originalPicture(document, frameIndex), scale),
                      originalSize.width * scale, originalLeft, originalTop);
            }
            for (uint32_t step : steps) {
                flush(timeMs + step, false);
                document::Rgb565Frame picture = composed;
                applyColorRotations(document,
                                    frameIndex,
                                    options.plane,
                                    static_cast<int>(step),
                                    picture,
                                    &notRotating);
                place(canvas, width, scaledPicture(picture, scale), planeSize.width * scale,
                      colorizedLeft, colorizedTop);
                pending = canvas;
            }
            timeMs += duration;
            if (progress) {
                progress->done = n + 1;
            }
        }
        flush(timeMs, true);
        if (!writer.finish()) {
            result.error = "the GIF could not be written";
            return result;
        }
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace colorizing::app

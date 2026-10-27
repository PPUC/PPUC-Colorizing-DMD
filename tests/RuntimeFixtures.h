// Synthetic projects for the tests of the runtime preview and of the editor's renderer: one
// builder for all three plane layouts, with every kind of content libserum renders.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "document/ProjectDocument.h"
#include "document/ProjectSnapshot.h"
#include "render/ProjectGeometry.h"
#include "runtime/RuntimePreview.h"

namespace colorizing::runtime_fixtures {

using document::BackgroundRecord;
using document::FrameBackgroundData;
using document::FrameDynamicData;
using document::FrameSpriteReference;
using document::IndexedFrame;
using document::ProjectFrameSnapshot;
using document::ProjectSnapshot;
using document::Rgb565Frame;
using document::SpriteDetectionArea;
using document::SpriteRecord;
using document::kNoTriggerId;
using document::makeIndexedFrame;
using document::makeRgb565Frame;
using render::PlaneLayout;
using render::ProjectGeometry;
using render::Resolution;
using render::Size;
using runtime::PlaneRequest;

constexpr uint32_t kColorCount = 4;
constexpr Size kSpriteSize{256, 64};
constexpr uint8_t kT = 255;
constexpr uint16_t kShadowColor = 0xFACE;

inline std::size_t pixels(Size size)
{
    return static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height);
}

// No period of 2 and different for every frame, so frames have different hashes.
inline uint8_t shade(std::size_t frame, int x, int y)
{
    return static_cast<uint8_t>((x * 5 + y * 3 + ((x * y) % 7) +
                                 ((x / 8 + y / 4) % (2 * static_cast<int>(frame) + 3))) %
                                static_cast<int>(kColorCount));
}

inline uint16_t colorSd(std::size_t frame, std::size_t index)
{
    return static_cast<uint16_t>(0x1000 + ((index + frame * 977) % 0x3000));
}

inline uint16_t colorHd(std::size_t frame, std::size_t index)
{
    return static_cast<uint16_t>(0x4000 + ((index + frame * 977) % 0x4000));
}

inline uint16_t backgroundSd(std::size_t index)
{
    return static_cast<uint16_t>(0x8000 + (index % 0x3000));
}

inline uint16_t backgroundHd(std::size_t index)
{
    return static_cast<uint16_t>(0xB000 + (index % 0x4000));
}

inline uint16_t dynamicColor(std::size_t set, uint8_t value)
{
    return static_cast<uint16_t>(0xF100 + set * 16 + value);
}

inline bool extraIsNative(const ProjectGeometry& geometry)
{
    return geometry.nativePlane() == Resolution::Hd;
}

inline PlaneRequest nativeRequest(const ProjectGeometry& geometry)
{
    return geometry.layout() == PlaneLayout::Ordinary ? PlaneRequest::Rows32
                                                      : PlaneRequest::Rows64;
}

inline ProjectSnapshot makeProject(PlaneLayout layout, std::size_t frameCount)
{
    ProjectSnapshot snapshot;
    snapshot.romName = "runtime";
    snapshot.colorCount = kColorCount;
    snapshot.geometry = ProjectGeometry(layout);
    const Size rom = snapshot.geometry.romFrameSize();
    const Size sd = snapshot.geometry.planeSize(Resolution::Sd);
    const Size hd = snapshot.geometry.planeSize(Resolution::Hd);
    for (std::size_t f = 0; f < frameCount; ++f) {
        ProjectFrameSnapshot frame;
        frame.originalSd = makeIndexedFrame(rom);
        for (int y = 0; y < rom.height; ++y) {
            for (int x = 0; x < rom.width; ++x) {
                frame.originalSd.pixels[static_cast<std::size_t>(y) * rom.width + x] =
                    shade(f, x, y);
            }
        }
        frame.colorizedSd = makeRgb565Frame(sd);
        for (std::size_t i = 0; i < frame.colorizedSd.pixels.size(); ++i) {
            frame.colorizedSd.pixels[i] = colorSd(f, i);
        }
        if (layout == PlaneLayout::NativeExtra) {
            frame.colorizedHd = makeRgb565Frame(hd);
            for (std::size_t i = 0; i < frame.colorizedHd->pixels.size(); ++i) {
                frame.colorizedHd->pixels[i] = colorHd(f, i);
            }
        }
        frame.comparisonMaskId = 255;
        frame.durationMs = 100;
        snapshot.frames.push_back(std::move(frame));
    }
    return snapshot;
}

inline Rgb565Frame& nativeColors(ProjectSnapshot& snapshot, std::size_t frame)
{
    return extraIsNative(snapshot.geometry) ? *snapshot.frames[frame].colorizedHd
                                            : snapshot.frames[frame].colorizedSd;
}

inline FrameDynamicData& addDynamicData(ProjectSnapshot& snapshot, std::size_t frame)
{
    FrameDynamicData data;
    data.maskSd = makeIndexedFrame(snapshot.geometry.planeSize(Resolution::Sd), 255);
    data.maskHd = makeIndexedFrame(snapshot.geometry.planeSize(Resolution::Hd), 255);
    data.colorsSd.assign(32 * kColorCount, 0);
    data.colorsHd.assign(32 * kColorCount, 0);
    data.shadowDirectionsSd.assign(32, 0);
    data.shadowDirectionsHd.assign(32, 0);
    data.shadowColorsSd.assign(32, 0);
    data.shadowColorsHd.assign(32, 0);
    for (std::size_t set = 0; set < 32; ++set) {
        for (uint8_t value = 0; value < kColorCount; ++value) {
            data.colorsSd[set * kColorCount + value] = dynamicColor(set, value);
            data.colorsHd[set * kColorCount + value] = dynamicColor(set, value);
        }
    }
    snapshot.frames[frame].dynamicData = std::move(data);
    return *snapshot.frames[frame].dynamicData;
}

inline IndexedFrame& nativeDynamicMask(ProjectSnapshot& snapshot, std::size_t frame)
{
    FrameDynamicData& data = *snapshot.frames[frame].dynamicData;
    return extraIsNative(snapshot.geometry) ? data.maskHd : data.maskSd;
}

inline void setNativeShadow(ProjectSnapshot& snapshot, std::size_t frame, std::size_t set,
                     uint8_t directions)
{
    FrameDynamicData& data = *snapshot.frames[frame].dynamicData;
    const bool extra = extraIsNative(snapshot.geometry);
    (extra ? data.shadowDirectionsHd : data.shadowDirectionsSd)[set] = directions;
    (extra ? data.shadowColorsHd : data.shadowColorsSd)[set] = kShadowColor;
}

inline std::size_t addBackground(ProjectSnapshot& snapshot)
{
    BackgroundRecord background;
    background.colorizedSd = makeRgb565Frame(snapshot.geometry.planeSize(Resolution::Sd));
    for (std::size_t i = 0; i < background.colorizedSd.pixels.size(); ++i) {
        background.colorizedSd.pixels[i] = backgroundSd(i + snapshot.backgrounds.size());
    }
    if (snapshot.geometry.layout() != PlaneLayout::NativeOriginal) {
        background.colorizedHd = makeRgb565Frame(snapshot.geometry.planeSize(Resolution::Hd));
        for (std::size_t i = 0; i < background.colorizedHd->pixels.size(); ++i) {
            background.colorizedHd->pixels[i] = backgroundHd(i + snapshot.backgrounds.size());
        }
    }
    snapshot.backgrounds.push_back(std::move(background));
    return snapshot.backgrounds.size() - 1;
}

inline void assignBackground(ProjectSnapshot& snapshot, std::size_t frame, std::size_t background)
{
    FrameBackgroundData data;
    data.backgroundId = static_cast<uint16_t>(background);
    data.maskSd = makeIndexedFrame(snapshot.geometry.planeSize(Resolution::Sd), 1);
    data.maskHd = makeIndexedFrame(snapshot.geometry.planeSize(Resolution::Hd), 1);
    snapshot.frames[frame].backgroundData = std::move(data);
}

inline const Rgb565Frame& nativeBackground(const ProjectSnapshot& snapshot, std::size_t background)
{
    return extraIsNative(snapshot.geometry) ? *snapshot.backgrounds[background].colorizedHd
                                            : snapshot.backgrounds[background].colorizedSd;
}

constexpr int kSpriteWidth = 6;
constexpr int kSpriteHeight = 4;
constexpr uint8_t kSpritePixels[kSpriteHeight][kSpriteWidth] = {{1, 1, 2, 3, 3, 1},
                                                              {2, 1, 2, 3, 1, 3},
                                                              {3, 3, kT, 1, 2, 2},
                                                              {1, 2, 1, 1, 3, kT}};
constexpr int kShapeWidth = 5;
constexpr int kShapeHeight = 3;
constexpr uint8_t kShapePixels[kShapeHeight][kShapeWidth] = {{1, 0, 1, 1, 0},
                                                           {1, 1, 0, 1, 1},
                                                           {0, 1, 1, 1, 0}};

inline uint16_t spriteColor(std::size_t sprite, int x, int y)
{
    return static_cast<uint16_t>(0xF400 + sprite * 0x80 + y * 16 + x);
}

inline uint16_t spriteDynamicColor(std::size_t sprite, uint8_t value)
{
    return static_cast<uint16_t>(0xF800 + sprite * 16 + value);
}

inline SpriteRecord emptySprite()
{
    SpriteRecord sprite;
    sprite.originalSd = makeIndexedFrame(kSpriteSize, kT);
    sprite.colorizedSd = makeRgb565Frame(kSpriteSize);
    sprite.maskHd = makeIndexedFrame(kSpriteSize, kT);
    sprite.colorizedHd = makeRgb565Frame(kSpriteSize);
    sprite.detectionDwords.assign(4, 0xffffffff);
    sprite.detectionDwordPositions.assign(4, 0xffff);
    sprite.detectionAreas.assign(4, SpriteDetectionArea{});
    sprite.dynamicMaskSd = makeIndexedFrame(kSpriteSize, 255);
    sprite.dynamicMaskHd = makeIndexedFrame(kSpriteSize, 255);
    sprite.dynamicColorsSd.assign(9 * kColorCount, 0);
    sprite.dynamicColorsHd.assign(9 * kColorCount, 0);
    return sprite;
}

// The sprite's art goes to the plane libserum draws it on: the native one.
inline void setSpritePixel(SpriteRecord& sprite, bool extraNative, std::size_t id, int x, int y,
                    uint8_t original)
{
    const std::size_t index = static_cast<std::size_t>(y) * 256 + x;
    sprite.originalSd.pixels[index] = original;
    if (extraNative) {
        sprite.maskHd.pixels[index] = original;
        sprite.colorizedHd.pixels[index] = spriteColor(id, x, y);
    } else {
        sprite.colorizedSd.pixels[index] = spriteColor(id, x, y);
    }
}

inline std::size_t addSprite(ProjectSnapshot& snapshot)
{
    const bool extra = extraIsNative(snapshot.geometry);
    const std::size_t id = snapshot.sprites.size();
    SpriteRecord sprite = emptySprite();
    sprite.hasHd = extra;
    for (int y = 0; y < kSpriteHeight; ++y) {
        for (int x = 0; x < kSpriteWidth; ++x) {
            if (kSpritePixels[y][x] != kT) {
                setSpritePixel(sprite, extra, id, x, y, kSpritePixels[y][x]);
            }
        }
    }
    sprite.detectionAreas[0] = SpriteDetectionArea{0, 0, kSpriteWidth, kSpriteHeight};
    sprite.detectionDwords[0] = 1u | (2u << 8) | (3u << 16) | (1u << 24);
    sprite.detectionDwordPositions[0] = 1 * 256 + 1;
    snapshot.sprites.push_back(std::move(sprite));
    return id;
}

inline std::size_t addShapeSprite(ProjectSnapshot& snapshot)
{
    const bool extra = extraIsNative(snapshot.geometry);
    const std::size_t id = snapshot.sprites.size();
    SpriteRecord sprite = emptySprite();
    sprite.hasHd = extra;
    sprite.shapeMode = 1;
    for (int y = 0; y < kShapeHeight; ++y) {
        for (int x = 0; x < kShapeWidth; ++x) {
            setSpritePixel(sprite, extra, id, x, y, kShapePixels[y][x] ? uint8_t{3} : uint8_t{0});
        }
    }
    sprite.detectionAreas[0] = SpriteDetectionArea{0, 0, kShapeWidth, kShapeHeight};
    sprite.detectionDwords[0] = 3u | (3u << 8) | (3u << 24);
    sprite.detectionDwordPositions[0] = 1 * 256 + 0;
    snapshot.sprites.push_back(std::move(sprite));
    return id;
}

// A dark ROM frame with one lit pixel that tells frames apart.
inline void makeDark(ProjectSnapshot& snapshot, std::size_t frame)
{
    IndexedFrame& original = snapshot.frames[frame].originalSd;
    std::fill(original.pixels.begin(), original.pixels.end(), uint8_t{0});
    original.pixels[original.pixels.size() - 1 - frame] = 2;
}

inline void placeSprite(ProjectSnapshot& snapshot, std::size_t frame, int sx, int sy)
{
    IndexedFrame& original = snapshot.frames[frame].originalSd;
    for (int y = 0; y < kSpriteHeight; ++y) {
        for (int x = 0; x < kSpriteWidth; ++x) {
            original.pixels[static_cast<std::size_t>(sy + y) * original.size.width + sx + x] =
                kSpritePixels[y][x] == kT ? uint8_t{3} : kSpritePixels[y][x];
        }
    }
}

inline void placeShapeSprite(ProjectSnapshot& snapshot, std::size_t frame, int sx, int sy)
{
    IndexedFrame& original = snapshot.frames[frame].originalSd;
    for (int y = 0; y < kShapeHeight; ++y) {
        for (int x = 0; x < kShapeWidth; ++x) {
            original.pixels[static_cast<std::size_t>(sy + y) * original.size.width + sx + x] =
                kShapePixels[y][x] ? static_cast<uint8_t>(1 + (x + y) % 3) : uint8_t{0};
        }
    }
}

inline void setSpriteSlot(ProjectSnapshot& snapshot, std::size_t frame, std::size_t slot,
                   std::size_t sprite, int minX, int minY, int maxX, int maxY)
{
    std::vector<FrameSpriteReference>& references = snapshot.frames[frame].spriteReferences;
    references.resize(colorizing::document::kSpritesPerFrame);
    references[slot] = FrameSpriteReference{static_cast<uint16_t>(sprite),
                                            static_cast<uint16_t>(minX),
                                            static_cast<uint16_t>(minY),
                                            static_cast<uint16_t>(maxX),
                                            static_cast<uint16_t>(maxY)};
}

// A project with every kind of content, for comparing ways of rendering it.
inline ProjectSnapshot makeFullProject(PlaneLayout layout)
{
    ProjectSnapshot snapshot = makeProject(layout, 9);
    const Size rom = snapshot.geometry.romFrameSize();
    snapshot.comparisonMasks.assign(3, makeIndexedFrame(rom, 0));
    for (int x = 0; x < 16; ++x) {
        snapshot.comparisonMasks[1].pixels[static_cast<std::size_t>(x)] = 1;
        snapshot.comparisonMasks[2].pixels[pixels(rom) - 1 - static_cast<std::size_t>(x)] = 1;
    }
    for (int i = 0; i < 3; ++i) {
        addBackground(snapshot);
    }
    addSprite(snapshot);
    addShapeSprite(snapshot);
    addSprite(snapshot);
    const bool extra = extraIsNative(snapshot.geometry);
    for (std::size_t id : {std::size_t{0}, std::size_t{2}}) {
        SpriteRecord& record = snapshot.sprites[id];
        (extra ? record.dynamicMaskHd : record.dynamicMaskSd).pixels[1 * 256 + 2] = 0;
        auto& colors = extra ? record.dynamicColorsHd : record.dynamicColorsSd;
        for (uint8_t value = 0; value < kColorCount; ++value) {
            colors[value] = spriteDynamicColor(id, value);
        }
    }

    snapshot.frames[1].comparisonMaskId = 2;
    assignBackground(snapshot, 1, 2);

    addDynamicData(snapshot, 2);
    IndexedFrame& mask = nativeDynamicMask(snapshot, 2);
    for (int y = 6; y < 18; ++y) {
        for (int x = 20; x < 70; ++x) {
            mask.pixels[static_cast<std::size_t>(y) * mask.size.width + x] = 4;
        }
    }
    setNativeShadow(snapshot, 2, 4, 0x10 | 0x08);
    snapshot.frames[2].comparisonMaskId = 1;

    makeDark(snapshot, 3);
    placeSprite(snapshot, 3, 30, 8);
    setSpriteSlot(snapshot, 3, 0, 2, 30, 8, 35, 11);

    makeDark(snapshot, 4);
    placeShapeSprite(snapshot, 4, 90, 20);
    placeSprite(snapshot, 4, 12, 3);
    setSpriteSlot(snapshot, 4, 0, 1, 80, 10, 110, 30);
    setSpriteSlot(snapshot, 4, 1, 0, 2, 0, 60, 20);
    assignBackground(snapshot, 4, 0);

    std::vector<uint16_t> rotations(4 * 64, 0);
    rotations[0] = 2;
    rotations[1] = 500;
    rotations[2] = nativeColors(snapshot, 5).pixels[3];
    rotations[3] = 0x0777;
    (extra ? snapshot.frames[5].colorRotationsHd : snapshot.frames[5].colorRotationsSd) =
        rotations;

    if (layout == PlaneLayout::Ordinary) {
        // HD frames, one of them with a background that has HD too.
        const Size hd = snapshot.geometry.planeSize(Resolution::Hd);
        for (std::size_t frame : {std::size_t{1}, std::size_t{6}}) {
            snapshot.frames[frame].colorizedHd = makeRgb565Frame(hd);
            for (std::size_t i = 0; i < pixels(hd); ++i) {
                snapshot.frames[frame].colorizedHd->pixels[i] = colorHd(frame, i);
            }
        }
    }
    if (layout == PlaneLayout::NativeExtra) {
        // Frame 0 has no 256x64 data: libserum then drops the 256x64 dynamic data of sprite 0,
        // and keeps that of sprite 2, whose frame 2 has it.
        snapshot.frames[0].colorizedHd.reset();
    }
    return snapshot;
}

}  // namespace colorizing::runtime_fixtures

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace colorizing::document {

// A zone of a picture as a part of it: left, top, width and height in millionths of the
// picture's width and height, so that it fits any picture at any size.
struct PictureZone {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 1000000;
    uint32_t height = 1000000;

    friend bool operator==(const PictureZone& a, const PictureZone& b)
    {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
};

// A saved zone of the Images window (the Win32 editor's crop zone save): the zone the
// selection gets, the end zone of a scroll, and the picture's adjustments. Editor data only,
// kept in the .cRPX. The Win32 editor's saves (in screen pixels of its window, which cannot be
// carried over) stay in the editor metadata as they were and are not these.
struct SavedImageZone {
    std::string name;
    PictureZone start;
    std::optional<PictureZone> end;
    int8_t brightness = 0;
    int8_t contrast = 0;
    uint8_t blur = 0;

    friend bool operator==(const SavedImageZone& a, const SavedImageZone& b)
    {
        return a.name == b.name && a.start == b.start && a.end == b.end &&
               a.brightness == b.brightness && a.contrast == b.contrast && a.blur == b.blur;
    }
};

// As many as the Win32 editor keeps.
constexpr std::size_t kMaxSavedImageZones = 256;

}  // namespace colorizing::document

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace colorizing::document {

// The identity of a component of a project (a frame, comparison mask, background, sprite or
// palette): a random UUID (version 4), given once and kept for the component's life, also
// when its number in the project changes or it goes to another project (packages, merges).
struct ComponentId {
    uint64_t high = 0;
    uint64_t low = 0;

    [[nodiscard]] static ComponentId generate();
    [[nodiscard]] bool isNull() const { return high == 0 && low == 0; }
    // 8-4-4-4-12 lowercase hexadecimal digits.
    [[nodiscard]] std::string toString() const;
    // Nothing for text that is not one.
    [[nodiscard]] static std::optional<ComponentId> fromString(std::string_view text);

    auto operator<=>(const ComponentId&) const = default;
};

struct ComponentIdHash {
    [[nodiscard]] std::size_t operator()(const ComponentId& id) const
    {
        return static_cast<std::size_t>(id.high ^ (id.low * 0x9e3779b97f4a7c15ULL));
    }
};

// What a project keeps about a component besides its content: who changed it last and when,
// and which revision of it this is. The revision changes with every saved change of the
// content (a new UUID), so that two copies of a project can tell which side changed it.
struct ComponentMeta {
    ComponentId id;
    // Null until the component's first saved revision.
    ComponentId revision;
    std::string author;
    // UTC milliseconds since 1970; 0 when unknown.
    int64_t modifiedMs = 0;
    // A hash of the content at that revision, to tell whether it changed since.
    uint64_t contentHash = 0;

    bool operator==(const ComponentMeta&) const = default;
};

enum class ComponentKind : uint8_t {
    Frame,
    ComparisonMask,
    Background,
    Sprite,
    Palette,
};

// The change information a save gave a component (project::saveProjectFile()), for the
// document to take over (ProjectDocument::adoptComponentMeta()).
struct StampedMeta {
    ComponentKind kind = ComponentKind::Frame;
    ComponentMeta meta;
};

// A 64-bit hash of the bytes, the same on every platform; for telling whether a component
// changed, not for security.
[[nodiscard]] uint64_t contentHash(std::string_view bytes);

}  // namespace colorizing::document

#pragma once

// Project files for tests: the fixed legacy files of tests/fixtures/legacy, which the readers are
// tested with (the editor writes none), and a project through the entries of a project file, the
// way the editor saves and opens one.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "document/ProjectSnapshot.h"
#include "project/ProjectFile.h"

namespace colorizing::test_files {

using Entries = std::map<std::string, std::string>;

[[nodiscard]] inline std::filesystem::path legacyFixturePath(const std::string& name)
{
    return std::filesystem::path(COLORIZING_LEGACY_FIXTURES_DIR) / name;
}

[[nodiscard]] inline std::vector<uint8_t> legacyFixtureBytes(const std::string& name)
{
    std::ifstream stream(legacyFixturePath(name), std::ios::binary);
    if (!stream) {
        throw std::runtime_error("no such fixture: " + name);
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), {});
}

// The snapshot with an ID for each component without one, the same each time, so that two
// snapshots of one content have the same entries. A document's snapshot has its own.
[[nodiscard]] inline document::ProjectSnapshot withComponentIds(document::ProjectSnapshot snapshot)
{
    const auto give = [](document::ComponentMeta& meta, uint64_t kind, std::size_t index) {
        if (meta.id.isNull()) {
            meta.id = document::ComponentId{kind, index + 1};
        }
    };
    for (std::size_t i = 0; i < snapshot.frames.size(); ++i) {
        give(snapshot.frames[i].meta, 1, i);
    }
    snapshot.comparisonMaskMeta.resize(snapshot.comparisonMasks.size());
    for (std::size_t i = 0; i < snapshot.comparisonMaskMeta.size(); ++i) {
        give(snapshot.comparisonMaskMeta[i], 2, i);
    }
    for (std::size_t i = 0; i < snapshot.backgrounds.size(); ++i) {
        give(snapshot.backgrounds[i].meta, 3, i);
    }
    for (std::size_t i = 0; i < snapshot.sprites.size(); ++i) {
        give(snapshot.sprites[i].meta, 4, i);
    }
    for (std::size_t i = 0; i < snapshot.palettes.size(); ++i) {
        give(snapshot.palettes[i].meta, 5, i);
    }
    if (snapshot.projectId.isNull()) {
        snapshot.projectId = document::ComponentId{6, 1};
    }
    return snapshot;
}

[[nodiscard]] inline Entries projectEntries(const document::ProjectSnapshot& snapshot)
{
    Entries entries;
    project::forEachProjectEntry(withComponentIds(snapshot), {"test"},
                                 [&entries](const std::string& name, const std::string& bytes) {
                                     entries.emplace(name, bytes);
                                 });
    return entries;
}

[[nodiscard]] inline document::ProjectSnapshot fromProjectEntries(const Entries& entries)
{
    std::string error;
    std::optional<document::ProjectSnapshot> snapshot = project::readProjectEntries(
        [&entries](const std::string& name) -> std::optional<std::string> {
            const auto found = entries.find(name);
            if (found == entries.end()) {
                return std::nullopt;
            }
            return found->second;
        },
        error);
    if (!snapshot) {
        throw std::runtime_error("the project entries do not read back: " + error);
    }
    return std::move(*snapshot);
}

}  // namespace colorizing::test_files

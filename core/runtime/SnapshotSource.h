#pragma once

// Inside the core only: it includes libserum's headers, which do not reach the targets that
// link the core.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

#include <serum-decode.h>

#include "document/ProjectSnapshot.h"
#include "legacy/LegacyProjectSerializer.h"

namespace colorizing::runtime {

static_assert(static_cast<std::size_t>(SERUM_BLOCK_COUNT) == legacy::kCromBlockCount,
              "libserum's blocks and the editor's are the same, in the same order");

// A project handed to libserum without a cROM (Serum_LoadFromSource()): every element of
// the blocks as writeLegacyCrom() writes it, with 16-bit sprite IDs, and the settings block's
// payload. Neither copied nor moved: libserum calls back into it.
class SnapshotSource {
public:
    explicit SnapshotSource(document::ProjectSnapshot snapshot)
        : m_snapshot(std::move(snapshot))
        , m_elements(m_snapshot, true)
    {
        const render::Size sd = m_snapshot.geometry.planeSize(render::Resolution::Sd);
        const render::Size hd = m_snapshot.geometry.planeSize(render::Resolution::Hd);
        std::snprintf(m_source.romName, sizeof(m_source.romName), "%s",
                      m_snapshot.romName.c_str());
        m_source.width = static_cast<uint32_t>(sd.width);
        m_source.height = static_cast<uint32_t>(sd.height);
        m_source.widthExtra = static_cast<uint32_t>(hd.width);
        m_source.heightExtra = static_cast<uint32_t>(hd.height);
        m_source.frames = static_cast<uint32_t>(m_snapshot.frames.size());
        m_source.colors = m_snapshot.colorCount;
        m_source.comparisonMasks = static_cast<uint32_t>(m_snapshot.comparisonMasks.size());
        m_source.sprites = static_cast<uint32_t>(m_snapshot.sprites.size());
        m_source.backgrounds = static_cast<uint16_t>(m_snapshot.backgrounds.size());
        m_source.is256x64 = m_snapshot.geometry.original256x64Flag() ? 1 : 0;
        m_source.spritesPerFrame = static_cast<uint32_t>(
            document::libserumLimits(m_snapshot.libserumTarget).spritesPerFrame);
        // The block without its magic and size.
        const std::vector<uint8_t> block = legacy::cromSettingsBlock(m_snapshot);
        if (block.size() > 8) {
            m_settings.assign(block.begin() + 8, block.end());
        }
        m_source.settings = m_settings.empty() ? nullptr : m_settings.data();
        m_source.settingsSize = m_settings.size();
        m_source.fill = &SnapshotSource::fill;
        m_source.userData = this;
    }
    SnapshotSource(const SnapshotSource&) = delete;
    SnapshotSource& operator=(const SnapshotSource&) = delete;

    [[nodiscard]] Serum_Frame_Struc* load(const char* scenes, uint8_t flags)
    {
        return Serum_LoadFromSource(&m_source, scenes, flags);
    }
    // The cROMc of the project at the path, as Serum_Load() makes it for its cROM
    // (Serum_ConcentrateFromSource()); nothing stays loaded.
    [[nodiscard]] bool writeConcentrate(const std::string& path)
    {
        return Serum_ConcentrateFromSource(&m_source, path.c_str());
    }
    // Why an element could not be given, empty when all could.
    [[nodiscard]] const std::string& error() const { return m_error; }

private:
    static void SERUM_CALLBACK fill(void* userData,
                                    Serum_Block block,
                                    uint32_t index,
                                    void* out,
                                    size_t size)
    {
        auto& self = *static_cast<SnapshotSource*>(userData);
        // Nothing may leave the callback: libserum's C API is between.
        try {
            self.m_elements.write(static_cast<legacy::CromBlock>(block), index, self.m_buffer);
            if (self.m_buffer.size() == size) {
                std::memcpy(out, self.m_buffer.data(), size);
                return;
            }
            if (self.m_error.empty()) {
                self.m_error = std::string("element of another size in block ") +
                               legacy::cromBlockName(static_cast<legacy::CromBlock>(block));
            }
        } catch (const std::exception& ex) {
            if (self.m_error.empty()) {
                self.m_error = ex.what();
            }
        }
        std::memset(out, 0, size);
    }

    document::ProjectSnapshot m_snapshot;
    legacy::CromElements m_elements;
    std::vector<uint8_t> m_settings;
    std::vector<uint8_t> m_buffer;
    std::string m_error;
    Serum_Colorization_Source m_source{};
};

}  // namespace colorizing::runtime

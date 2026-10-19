#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "document/ProjectDocument.h"

namespace colorizing::runtime {

// What libserum's frame identification depends on, for a whole project, without the project
// being loaded into libserum (Identify_Frame() in libserum's serum-decode.cpp).
//
// libserum identifies a ROM frame by order, not by content alone:
//
// - Every frame has a comparison mask and a shape mode; the pair is its "bucket". A frame's
//   hash is the CRC of its original under that mask and mode.
// - The search starts at the frame identified last (the first frame after a load) and walks
//   the frames in file order, wrapping around. The first time it meets a bucket it hashes the
//   ROM frame with that bucket's mask and mode and looks the hash up among the bucket's
//   frames. The first bucket that has such a frame decides.
// - Of that bucket's frames with the hash it takes the one nearest at or after the frame it
//   met the bucket at, which is the first of them at or after the search start.
//
// So a ROM frame that several frames match (frames with the same original under a shared
// mask, or a frame of one mask that looks like a frame of another under the other's mask) is
// identified as different frames depending on what was identified before it, and frames that
// do not match it take part too, by deciding which bucket is met first.
//
// Scenes of a .pup.csv have a search of their own in libserum; nothing here knows them,
// like the rest of the runtime preview.
class FrameIdentification {
public:
    explicit FrameIdentification(const document::ProjectDocument& document);

    // ProjectDocument::identificationRevision() of the document this was built from.
    [[nodiscard]] uint64_t revision() const;
    [[nodiscard]] std::size_t frameCount() const;

    // Every frame libserum can identify the ROM frame as, ascending: the frames of every
    // bucket whose hash is the ROM frame's hash under that bucket.
    [[nodiscard]] std::vector<std::size_t> matches(const document::IndexedFrame& romFrame) const;

    // The frame libserum identifies the ROM frame as when its search starts at searchStart;
    // nothing if no frame matches.
    [[nodiscard]] std::optional<std::size_t> identify(const document::IndexedFrame& romFrame,
                                                      std::size_t searchStart) const;

    // The frames a colorization must hold, in file order, so that libserum identifies the
    // original of every frame of `played` as it does with the whole project loaded, for a
    // search that starts at searchStart or at any frame identified since:
    //
    //   1. searchStart and every frame that matches the original of a played frame
    //      (matches()): the frames a search can start at and the frames it can end at;
    //   2. for each frame of 1 and each bucket that has a frame in 1, the first frame of
    //      that bucket at or after it: the frame the search meets the bucket at.
    //
    // With these, a search from any frame of 1 meets the buckets that can match in the same
    // order and at the same frames as in the whole project, and finds the same frames with
    // the hash in each, so it ends at the same frame. Frames of 2 that are not in 1 are never
    // identified themselves. Ascending; `played` is part of the result.
    [[nodiscard]] std::vector<std::size_t> candidates(const document::ProjectDocument& document,
                                                      const std::vector<std::size_t>& played,
                                                      std::size_t searchStart) const;
    // Step 2 alone: `starts` are the frames of step 1.
    [[nodiscard]] std::vector<std::size_t> candidatesOf(const std::set<std::size_t>& starts) const;

private:
    struct Bucket {
        // 255: no mask.
        uint8_t maskId = 255;
        uint8_t shapeMode = 0;
        // Ascending.
        std::vector<std::size_t> frames;
    };

    [[nodiscard]] uint32_t hashIn(const Bucket& bucket,
                                  const document::IndexedFrame& romFrame) const;
    // The bucket's frames with the hash; nullptr if there is none.
    [[nodiscard]] const std::vector<std::size_t>* framesWithHash(std::size_t bucketIndex,
                                                                 uint32_t hash) const;
    // The first frame of the sorted list at or after `from`, wrapping around.
    [[nodiscard]] static std::size_t firstAtOrAfter(const std::vector<std::size_t>& frames,
                                                    std::size_t from);
    // How far the search has walked when it reaches `frame` from `from`.
    [[nodiscard]] std::size_t distance(std::size_t from, std::size_t frame) const;

    uint64_t m_revision = 0;
    std::size_t m_frameCount = 0;
    std::vector<document::IndexedFrame> m_masks;
    std::vector<Bucket> m_buckets;
    std::vector<std::size_t> m_bucketOfFrame;
    // (bucket, hash) -> frames, ascending.
    std::unordered_map<uint64_t, std::vector<std::size_t>> m_framesBySignature;
};

}  // namespace colorizing::runtime

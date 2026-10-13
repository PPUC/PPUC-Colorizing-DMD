#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "document/ProjectDocument.h"
#include "edit/EditCommand.h"

namespace colorizing::edit {

// How the original of a frame is compared: without the pixels a comparison mask covers,
// and in shape mode with every shade above 1 taken as 1.
struct ComparisonRule {
    // document::kNoComparisonMaskId: no mask.
    uint8_t maskId = document::kNoComparisonMaskId;
    bool shapeMode = false;
};

// The rule the runtime identifies the frame by. A mask ID without a mask counts as no mask
// and only the stored value 1 as shape mode, as in a saved project.
[[nodiscard]] ComparisonRule comparisonRuleOf(const document::ProjectDocument& document,
                                              std::size_t frameIndex);

// The Win32 editor lists and counts no more same frames than this.
constexpr std::size_t kMaxListedSameFrames = 10000;

struct SameFrameCounts {
    // document::kNoComparisonMaskId for the line without a mask.
    uint8_t maskId = document::kNoComparisonMaskId;
    std::size_t fullComparison = 0;
    std::size_t shapeMode = 0;
};

// Which frames are "the same" under a rule: the frames whose original has the same hash
// under it (legacy::legacyFrameHash), which is exactly what the runtime cannot tell apart
// with that rule. The Win32 editor compares the pixels instead; the two differ only for a
// CRC collision.
//
// Keeps the hashes of all frames per rule until the document's identificationRevision()
// changes, so only the first question about a rule costs a pass over the originals.
class SameFrameFinder {
public:
    // The frames that are the same as the frame under the rule, ascending, without the
    // frame itself.
    [[nodiscard]] std::vector<std::size_t> sameFrames(const document::ProjectDocument& document,
                                                      std::size_t frameIndex,
                                                      ComparisonRule rule);
    [[nodiscard]] std::size_t sameFrameCount(const document::ProjectDocument& document,
                                             std::size_t frameIndex,
                                             ComparisonRule rule);
    // The count for no mask and for every comparison mask of the project, each without and
    // with shape mode ("Update same list"). progress is called before each rule with the
    // number done and the total; returning false stops and returns what there is so far.
    [[nodiscard]] std::vector<SameFrameCounts> countsPerMask(
        const document::ProjectDocument& document,
        std::size_t frameIndex,
        const std::function<bool(std::size_t done, std::size_t total)>& progress = {});

    // The frames to delete so that no reference frame has a same frame left, as the Win32
    // editor's "delete same frames" goes about it: the references are taken in ascending
    // order; each one that is still there removes every other frame that is the same as it
    // under its own rule (comparisonRuleOf) and is not protected. A frame removed for an
    // earlier reference is no reference any more. Ascending; never all frames.
    [[nodiscard]] std::vector<std::size_t> duplicatesOf(
        const document::ProjectDocument& document,
        const std::vector<std::size_t>& referenceFrames,
        const std::vector<std::size_t>& protectedFrames);
    // With every frame as a reference.
    [[nodiscard]] std::vector<std::size_t> allDuplicates(
        const document::ProjectDocument& document,
        const std::vector<std::size_t>& protectedFrames);

private:
    struct RuleHashes {
        std::vector<uint32_t> ofFrame;
        // Ascending frames per hash.
        std::unordered_map<uint32_t, std::vector<std::size_t>> frames;
    };

    [[nodiscard]] const RuleHashes& hashes(const document::ProjectDocument& document,
                                           ComparisonRule rule);

    uint64_t m_revision = 0;
    std::map<std::pair<uint8_t, bool>, RuleHashes> m_hashes;
};

// 1 where the originals of the frames are not all the same, 0 where they are, in the size
// of the ROM frame ("spot common"). All 0 for fewer than two frames.
[[nodiscard]] document::IndexedFrame differingPixels(const document::ProjectDocument& document,
                                                     const std::vector<std::size_t>& frameIndices);

// The frames that have the comparison mask assigned, ascending;
// document::kNoComparisonMaskId gives the frames without a mask.
[[nodiscard]] std::vector<std::size_t> framesUsingComparisonMask(
    const document::ProjectDocument& document,
    uint8_t maskId);

// Makes the comparison mask equal to `mask` (any value but 0 masks). Not applied; nullptr
// if the mask is that already. Throws std::out_of_range for a mask that does not exist and
// std::invalid_argument for another size.
[[nodiscard]] std::unique_ptr<EditCommand> replaceComparisonMask(
    const document::ProjectDocument& document,
    std::size_t maskIndex,
    const document::IndexedFrame& mask);

}  // namespace colorizing::edit

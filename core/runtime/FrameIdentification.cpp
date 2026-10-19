#include "runtime/FrameIdentification.h"

#include <algorithm>

#include "legacy/LegacyProjectSerializer.h"

namespace colorizing::runtime {

namespace {

uint64_t signature(std::size_t bucketIndex, uint32_t hash)
{
    return (static_cast<uint64_t>(bucketIndex) << 32) | hash;
}

}  // namespace

FrameIdentification::FrameIdentification(const document::ProjectDocument& document)
    : m_revision(document.identificationRevision()),
      m_frameCount(document.frameCount())
{
    m_masks.reserve(document.comparisonMaskCount());
    for (std::size_t mask = 0; mask < document.comparisonMaskCount(); ++mask) {
        m_masks.push_back(document.comparisonMask(mask));
    }
    m_bucketOfFrame.reserve(m_frameCount);
    for (std::size_t frame = 0; frame < m_frameCount; ++frame) {
        // As the serializer writes it: a mask the project does not have is no mask.
        const uint8_t storedMask = document.comparisonMaskId(frame);
        const uint8_t maskId = storedMask < m_masks.size() ? storedMask : uint8_t{255};
        const uint8_t shapeMode = document.shapeComparisonMode(frame);
        std::size_t bucketIndex = 0;
        while (bucketIndex < m_buckets.size() && (m_buckets[bucketIndex].maskId != maskId ||
                                                  m_buckets[bucketIndex].shapeMode != shapeMode)) {
            ++bucketIndex;
        }
        if (bucketIndex == m_buckets.size()) {
            m_buckets.push_back(Bucket{maskId, shapeMode, {}});
        }
        m_buckets[bucketIndex].frames.push_back(frame);
        m_bucketOfFrame.push_back(bucketIndex);
        m_framesBySignature[signature(bucketIndex,
                                      hashIn(m_buckets[bucketIndex],
                                             document.originalFrame(frame)))]
            .push_back(frame);
    }
}

uint64_t FrameIdentification::revision() const
{
    return m_revision;
}

std::size_t FrameIdentification::frameCount() const
{
    return m_frameCount;
}

uint32_t FrameIdentification::hashIn(const Bucket& bucket,
                                     const document::IndexedFrame& romFrame) const
{
    // The hash the serializer stores for a frame, which is libserum's calc_crc32().
    return legacy::legacyFrameHash(romFrame,
                                   bucket.maskId < m_masks.size() ? &m_masks[bucket.maskId]
                                                                  : nullptr,
                                   bucket.shapeMode == 1);
}

const std::vector<std::size_t>* FrameIdentification::framesWithHash(std::size_t bucketIndex,
                                                                    uint32_t hash) const
{
    const auto found = m_framesBySignature.find(signature(bucketIndex, hash));
    return found == m_framesBySignature.end() ? nullptr : &found->second;
}

std::size_t FrameIdentification::firstAtOrAfter(const std::vector<std::size_t>& frames,
                                                std::size_t from)
{
    const auto found = std::lower_bound(frames.begin(), frames.end(), from);
    return found == frames.end() ? frames.front() : *found;
}

std::size_t FrameIdentification::distance(std::size_t from, std::size_t frame) const
{
    return frame >= from ? frame - from : m_frameCount - from + frame;
}

std::vector<std::size_t> FrameIdentification::matches(
    const document::IndexedFrame& romFrame) const
{
    std::vector<std::size_t> frames;
    for (std::size_t bucket = 0; bucket < m_buckets.size(); ++bucket) {
        if (const std::vector<std::size_t>* found =
                framesWithHash(bucket, hashIn(m_buckets[bucket], romFrame))) {
            frames.insert(frames.end(), found->begin(), found->end());
        }
    }
    std::sort(frames.begin(), frames.end());
    return frames;
}

std::optional<std::size_t> FrameIdentification::identify(const document::IndexedFrame& romFrame,
                                                         std::size_t searchStart) const
{
    if (m_frameCount == 0) {
        return std::nullopt;
    }
    const std::size_t start = searchStart < m_frameCount ? searchStart : 0;
    // Buckets in the order the walk from the search start meets them.
    std::vector<std::pair<std::size_t, std::size_t>> order;
    order.reserve(m_buckets.size());
    for (std::size_t bucket = 0; bucket < m_buckets.size(); ++bucket) {
        order.emplace_back(distance(start, firstAtOrAfter(m_buckets[bucket].frames, start)),
                           bucket);
    }
    std::sort(order.begin(), order.end());
    for (const auto& [walked, bucket] : order) {
        const std::vector<std::size_t>* found =
            framesWithHash(bucket, hashIn(m_buckets[bucket], romFrame));
        if (!found) {
            continue;
        }
        // The nearest at or after the frame the bucket was met at.
        return firstAtOrAfter(*found, (start + walked) % m_frameCount);
    }
    return std::nullopt;
}

std::vector<std::size_t> FrameIdentification::candidates(
    const document::ProjectDocument& document,
    const std::vector<std::size_t>& played,
    std::size_t searchStart) const
{
    std::set<std::size_t> starts;
    if (searchStart < m_frameCount) {
        starts.insert(searchStart);
    }
    for (std::size_t frame : played) {
        const std::vector<std::size_t> matching = matches(document.originalFrame(frame));
        starts.insert(matching.begin(), matching.end());
        // Its own bucket matches it.
        starts.insert(frame);
    }
    return candidatesOf(starts);
}

std::vector<std::size_t> FrameIdentification::candidatesOf(
    const std::set<std::size_t>& starts) const
{
    std::set<std::size_t> buckets;
    for (std::size_t frame : starts) {
        if (frame < m_bucketOfFrame.size()) {
            buckets.insert(m_bucketOfFrame[frame]);
        }
    }
    std::set<std::size_t> frames;
    for (std::size_t start : starts) {
        if (start >= m_frameCount) {
            continue;
        }
        frames.insert(start);
        for (std::size_t bucket : buckets) {
            frames.insert(firstAtOrAfter(m_buckets[bucket].frames, start));
        }
    }
    return std::vector<std::size_t>(frames.begin(), frames.end());
}

}  // namespace colorizing::runtime

#include "edit/SameFrames.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "legacy/LegacyProjectSerializer.h"

namespace colorizing::edit {

namespace {

const document::IndexedFrame* maskOf(const document::ProjectDocument& document,
                                     ComparisonRule rule)
{
    return rule.maskId < document.comparisonMaskCount() ? &document.comparisonMask(rule.maskId)
                                                        : nullptr;
}

}  // namespace

ComparisonRule comparisonRuleOf(const document::ProjectDocument& document, std::size_t frameIndex)
{
    const uint8_t maskId = document.comparisonMaskId(frameIndex);
    return ComparisonRule{maskId < document.comparisonMaskCount() ? maskId
                                                                  : document::kNoComparisonMaskId,
                          document.shapeComparisonMode(frameIndex) == 1};
}

const SameFrameFinder::RuleHashes& SameFrameFinder::hashes(
    const document::ProjectDocument& document,
    ComparisonRule rule)
{
    if (m_revision != document.identificationRevision()) {
        m_hashes.clear();
        m_revision = document.identificationRevision();
    }
    if (rule.maskId >= document.comparisonMaskCount()) {
        rule.maskId = document::kNoComparisonMaskId;
    }
    const auto key = std::make_pair(rule.maskId, rule.shapeMode);
    const auto found = m_hashes.find(key);
    if (found != m_hashes.end()) {
        return found->second;
    }
    RuleHashes& entry = m_hashes[key];
    const document::IndexedFrame* mask = maskOf(document, rule);
    entry.ofFrame.reserve(document.frameCount());
    for (std::size_t frameIndex = 0; frameIndex < document.frameCount(); ++frameIndex) {
        const uint32_t hash =
            legacy::legacyFrameHash(document.originalFrame(frameIndex), mask, rule.shapeMode);
        entry.ofFrame.push_back(hash);
        entry.frames[hash].push_back(frameIndex);
    }
    return entry;
}

std::vector<std::size_t> SameFrameFinder::sameFrames(const document::ProjectDocument& document,
                                                     std::size_t frameIndex,
                                                     ComparisonRule rule)
{
    if (frameIndex >= document.frameCount()) {
        throw std::out_of_range("frame index out of range");
    }
    const RuleHashes& entry = hashes(document, rule);
    std::vector<std::size_t> frames = entry.frames.at(entry.ofFrame[frameIndex]);
    frames.erase(std::find(frames.begin(), frames.end(), frameIndex));
    return frames;
}

std::size_t SameFrameFinder::sameFrameCount(const document::ProjectDocument& document,
                                            std::size_t frameIndex,
                                            ComparisonRule rule)
{
    if (frameIndex >= document.frameCount()) {
        throw std::out_of_range("frame index out of range");
    }
    const RuleHashes& entry = hashes(document, rule);
    return entry.frames.at(entry.ofFrame[frameIndex]).size() - 1;
}

std::optional<std::size_t> SameFrameFinder::knownSameFrameCount(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    ComparisonRule rule) const
{
    if (m_revision != document.identificationRevision()) {
        return std::nullopt;
    }
    if (rule.maskId >= document.comparisonMaskCount()) {
        rule.maskId = document::kNoComparisonMaskId;
    }
    const auto found = m_hashes.find(std::make_pair(rule.maskId, rule.shapeMode));
    if (found == m_hashes.end() || frameIndex >= found->second.ofFrame.size()) {
        return std::nullopt;
    }
    const RuleHashes& entry = found->second;
    return entry.frames.at(entry.ofFrame[frameIndex]).size() - 1;
}

std::vector<SameFrameCounts> SameFrameFinder::countsPerMask(
    const document::ProjectDocument& document,
    std::size_t frameIndex,
    const std::function<bool(std::size_t done, std::size_t total)>& progress)
{
    std::vector<uint8_t> maskIds{document::kNoComparisonMaskId};
    for (std::size_t maskIndex = 0; maskIndex < document.comparisonMaskCount(); ++maskIndex) {
        maskIds.push_back(static_cast<uint8_t>(maskIndex));
    }
    std::vector<SameFrameCounts> counts;
    const std::size_t total = maskIds.size() * 2;
    for (uint8_t maskId : maskIds) {
        SameFrameCounts line;
        line.maskId = maskId;
        if (progress && !progress(counts.size() * 2, total)) {
            break;
        }
        line.fullComparison = sameFrameCount(document, frameIndex, {maskId, false});
        if (progress && !progress(counts.size() * 2 + 1, total)) {
            break;
        }
        line.shapeMode = sameFrameCount(document, frameIndex, {maskId, true});
        counts.push_back(line);
    }
    return counts;
}

std::vector<std::size_t> SameFrameFinder::duplicatesOf(
    const document::ProjectDocument& document,
    const std::vector<std::size_t>& referenceFrames,
    const std::vector<std::size_t>& protectedFrames)
{
    const std::size_t frameCount = document.frameCount();
    std::vector<std::size_t> references = referenceFrames;
    std::sort(references.begin(), references.end());
    references.erase(std::unique(references.begin(), references.end()), references.end());
    if (!references.empty() && references.back() >= frameCount) {
        throw std::out_of_range("frame index out of range");
    }
    std::vector<bool> isProtected(frameCount, false);
    for (std::size_t frameIndex : protectedFrames) {
        if (frameIndex < frameCount) {
            isProtected[frameIndex] = true;
        }
    }
    std::vector<bool> removed(frameCount, false);
    for (std::size_t reference : references) {
        if (removed[reference]) {
            continue;
        }
        const RuleHashes& entry = hashes(document, comparisonRuleOf(document, reference));
        for (std::size_t frameIndex : entry.frames.at(entry.ofFrame[reference])) {
            if (frameIndex != reference && !isProtected[frameIndex]) {
                removed[frameIndex] = true;
            }
        }
    }
    std::vector<std::size_t> frames;
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        if (removed[frameIndex]) {
            frames.push_back(frameIndex);
        }
    }
    return frames;
}

std::vector<std::size_t> SameFrameFinder::allDuplicates(
    const document::ProjectDocument& document,
    const std::vector<std::size_t>& protectedFrames)
{
    std::vector<std::size_t> references(document.frameCount());
    for (std::size_t frameIndex = 0; frameIndex < references.size(); ++frameIndex) {
        references[frameIndex] = frameIndex;
    }
    return duplicatesOf(document, references, protectedFrames);
}

document::IndexedFrame differingPixels(const document::ProjectDocument& document,
                                       const std::vector<std::size_t>& frameIndices)
{
    document::IndexedFrame differing = document::makeIndexedFrame(document.originalFrameSize());
    if (frameIndices.size() < 2) {
        return differing;
    }
    const auto& first = document.originalFrame(frameIndices.front()).pixels;
    for (std::size_t position = 1; position < frameIndices.size(); ++position) {
        const auto& other = document.originalFrame(frameIndices[position]).pixels;
        for (std::size_t index = 0; index < differing.pixels.size(); ++index) {
            if (first[index] != other[index]) {
                differing.pixels[index] = 1;
            }
        }
    }
    return differing;
}

document::IndexedFrame commonPixels(const document::ProjectDocument& document,
                                    const std::vector<std::size_t>& frameIndices)
{
    document::IndexedFrame common = differingPixels(document, frameIndices);
    if (frameIndices.size() < 2) {
        return common;
    }
    for (uint8_t& pixel : common.pixels) {
        pixel = pixel == 0 ? 1 : 0;
    }
    return common;
}

std::vector<std::size_t> framesUsingComparisonMask(const document::ProjectDocument& document,
                                                   uint8_t maskId)
{
    std::vector<std::size_t> frames;
    for (std::size_t frameIndex = 0; frameIndex < document.frameCount(); ++frameIndex) {
        const uint8_t assigned = comparisonRuleOf(document, frameIndex).maskId;
        if (assigned == maskId) {
            frames.push_back(frameIndex);
        }
    }
    return frames;
}

std::unique_ptr<EditCommand> replaceComparisonMask(const document::ProjectDocument& document,
                                                   std::size_t maskIndex,
                                                   const document::IndexedFrame& mask)
{
    const document::IndexedFrame& current = document.comparisonMask(maskIndex);
    if (mask.size != current.size || mask.pixels.size() != current.pixels.size()) {
        throw std::invalid_argument("the mask has not the size of the ROM frame");
    }
    std::vector<MaskPixelChange> changes;
    for (int y = 0; y < current.size.height; ++y) {
        for (int x = 0; x < current.size.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) *
                                          static_cast<std::size_t>(current.size.width) +
                                      static_cast<std::size_t>(x);
            const uint8_t newValue = mask.pixels[index] != 0 ? uint8_t{1} : uint8_t{0};
            if (current.pixels[index] != newValue) {
                changes.push_back({x, y, current.pixels[index], newValue});
            }
        }
    }
    if (changes.empty()) {
        return nullptr;
    }
    return std::make_unique<MaskEditCommand>(EditKind::ComparisonMask, maskIndex,
                                             render::Resolution::Sd, std::move(changes));
}

}  // namespace colorizing::edit

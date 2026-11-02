#include "edit/Bookmarks.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace colorizing::edit {

bool isBookmarked(const document::ProjectDocument& document, std::size_t frameIndex)
{
    const std::vector<document::ComponentId>& bookmarks = document.bookmarks();
    return !bookmarks.empty() &&
           std::find(bookmarks.begin(), bookmarks.end(), document.frameMeta(frameIndex).id) !=
               bookmarks.end();
}

bool toggleBookmark(document::ProjectDocument& document, std::size_t frameIndex)
{
    std::vector<document::ComponentId> bookmarks = document.bookmarks();
    const document::ComponentId id = document.frameMeta(frameIndex).id;
    const auto found = std::find(bookmarks.begin(), bookmarks.end(), id);
    const bool set = found == bookmarks.end();
    if (set) {
        bookmarks.push_back(id);
    } else {
        bookmarks.erase(found);
    }
    document.setBookmarks(std::move(bookmarks));
    return set;
}

std::vector<std::size_t> bookmarkedFrames(const document::ProjectDocument& document)
{
    std::vector<std::size_t> frames;
    if (document.bookmarks().empty()) {
        return frames;
    }
    for (std::size_t frame = 0; frame < document.frameCount(); ++frame) {
        if (isBookmarked(document, frame)) {
            frames.push_back(frame);
        }
    }
    return frames;
}

std::optional<std::size_t> adjacentBookmark(const std::vector<std::size_t>& frames,
                                            std::size_t from,
                                            bool forward)
{
    if (frames.empty()) {
        return std::nullopt;
    }
    if (forward) {
        const auto next = std::upper_bound(frames.begin(), frames.end(), from);
        return next != frames.end() ? *next : frames.front();
    }
    const auto next = std::lower_bound(frames.begin(), frames.end(), from);
    return next != frames.begin() ? *std::prev(next) : frames.back();
}

}  // namespace colorizing::edit

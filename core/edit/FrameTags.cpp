#include "edit/FrameTags.h"

#include <algorithm>

namespace colorizing::edit {

namespace {

std::vector<std::string> sortedUnique(std::vector<std::string> tags)
{
    tags.erase(std::remove(tags.begin(), tags.end(), std::string()), tags.end());
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    return tags;
}

}  // namespace

std::vector<std::string> parseFrameTags(const std::string& text)
{
    std::vector<std::string> tags;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find(',', start), text.size());
        const std::string part = text.substr(start, end - start);
        const std::size_t first = part.find_first_not_of(" \t");
        if (first != std::string::npos) {
            tags.push_back(part.substr(first, part.find_last_not_of(" \t") - first + 1));
        }
        start = end + 1;
    }
    return sortedUnique(std::move(tags));
}

std::string frameTagsText(const std::vector<std::string>& tags)
{
    std::string text;
    for (const std::string& tag : tags) {
        if (!text.empty()) {
            text += ", ";
        }
        text += tag;
    }
    return text;
}

std::vector<std::string> retaggedFrame(const std::vector<std::string>& tags,
                                       const std::vector<std::string>& before,
                                       const std::vector<std::string>& after)
{
    const auto contains = [](const std::vector<std::string>& list, const std::string& tag) {
        return std::find(list.begin(), list.end(), tag) != list.end();
    };
    std::vector<std::string> result;
    for (const std::string& tag : tags) {
        // Removed in the field: gone; kept, or not in the field at all: stays.
        if (!(contains(before, tag) && !contains(after, tag))) {
            result.push_back(tag);
        }
    }
    for (const std::string& tag : after) {
        if (!contains(before, tag)) {
            result.push_back(tag);
        }
    }
    return sortedUnique(std::move(result));
}

}  // namespace colorizing::edit

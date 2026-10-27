#include "document/DofEvents.h"

#include <cctype>
#include <charconv>
#include <limits>

namespace colorizing::document {

namespace {

// A whole decimal number of the part, nothing for any other text. negative: a leading '-' is
// taken.
std::optional<int32_t> numberOf(std::string_view text, bool negative)
{
    if (text.empty() || (!negative && text.front() == '-')) {
        return std::nullopt;
    }
    int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size() ||
        value < std::numeric_limits<int32_t>::min() ||
        value > std::numeric_limits<int32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<int32_t>(value);
}

std::optional<DofEvent> eventOf(std::string_view part)
{
    if (part.empty() || !std::isalpha(static_cast<unsigned char>(part.front()))) {
        return std::nullopt;
    }
    DofEvent event;
    event.type = static_cast<char>(std::toupper(static_cast<unsigned char>(part.front())));
    part.remove_prefix(1);
    const std::size_t equals = part.find('=');
    if (equals == std::string_view::npos) {
        return std::nullopt;
    }
    const std::optional<int32_t> number = numberOf(part.substr(0, equals), false);
    std::string_view values = part.substr(equals + 1);
    const std::size_t semicolon = values.find(';');
    const std::optional<int32_t> value = numberOf(values.substr(0, semicolon), true);
    if (!number || !value) {
        return std::nullopt;
    }
    event.number = *number;
    event.value = *value;
    if (semicolon != std::string_view::npos) {
        event.offValue = numberOf(values.substr(semicolon + 1), true);
        if (!event.offValue) {
            return std::nullopt;
        }
    }
    return event;
}

}  // namespace

bool isDofEventType(char type)
{
    return type >= 'A' && type <= 'Z';
}

std::string formatDofEvents(const DofEvents& events)
{
    std::string text;
    for (const DofEvent& event : events) {
        if (!text.empty()) {
            text += ' ';
        }
        text += event.type;
        text += std::to_string(event.number) + '=' + std::to_string(event.value);
        if (event.offValue) {
            text += ';' + std::to_string(*event.offValue);
        }
    }
    return text;
}

std::optional<DofEvents> parseDofEvents(std::string_view text, std::string* error)
{
    DofEvents events;
    std::size_t position = 0;
    while (position < text.size()) {
        const char c = text[position];
        if (c == ' ' || c == ',' || c == '\t') {
            ++position;
            continue;
        }
        std::size_t end = position;
        while (end < text.size() && text[end] != ' ' && text[end] != ',' && text[end] != '\t') {
            ++end;
        }
        const std::string_view part = text.substr(position, end - position);
        const std::optional<DofEvent> event = eventOf(part);
        if (!event) {
            if (error) {
                *error = std::string(part);
            }
            return std::nullopt;
        }
        events.push_back(*event);
        position = end;
    }
    return events;
}

}  // namespace colorizing::document

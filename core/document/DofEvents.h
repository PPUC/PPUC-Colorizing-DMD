#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace colorizing::document {

// DOF events: what a frame passes to the DirectOutput Framework (libdof) when it shows, to
// drive hardware such as a shaker motor. libserum hands them to the host
// (Serum_GetDofEvents()), which gives each to DOF::DataReceive(type, number, value); what an
// element does is DOF's configuration, not the project's.
struct DofEvent {
    // A table element type of libdof (TableElementTypeEnum): 'S' solenoid, 'L' lamp, 'W'
    // switch, 'G' GI string, 'M' mech, 'N' get mech, 'E' EM table, 'D' LED, 'C' score, 'B'
    // score digit, or another capital letter.
    char type = 'S';
    int32_t number = 0;
    int32_t value = 0;
    // Given to the element when the next frame shows, unless that frame sets the element
    // itself.
    std::optional<int32_t> offValue;

    friend bool operator==(const DofEvent& a, const DofEvent& b)
    {
        return a.type == b.type && a.number == b.number && a.value == b.value &&
               a.offValue == b.offValue;
    }
    friend bool operator!=(const DofEvent& a, const DofEvent& b) { return !(a == b); }
};

// A frame's events, in the order libserum emits them.
using DofEvents = std::vector<DofEvent>;

// Whether libserum takes the type: a capital letter.
[[nodiscard]] bool isDofEventType(char type);

// The events as the author writes them: "S5=255 L12=1;0", type, number, '=' and value, and
// after ';' the turn-off value.
[[nodiscard]] std::string formatDofEvents(const DofEvents& events);

// The events of such a text, separated by spaces or commas; a lowercase type is taken as
// capital. Nothing, with the reason in error, when a part is not an event.
[[nodiscard]] std::optional<DofEvents> parseDofEvents(std::string_view text,
                                                      std::string* error = nullptr);

}  // namespace colorizing::document

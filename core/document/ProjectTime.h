#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace colorizing::document {

// The time spent on a project, in all (EditorMetadata::timeElapsedMs, the cRP's TimeElapsed)
// and per author (EditorMetadata::authorTimeMs), while a session adds to it. The total keeps
// what no author is named for (projects of earlier editors, saves without a name); in a team
// project each author's time comes with their shared changes, and the larger one counts, so
// that every copy sums up the same.
class ProjectTime {
public:
    // The times as the project was opened.
    void open(uint64_t totalMs, std::map<std::string, uint64_t> authorsMs);

    // With the session's time; me: the author it counts for, empty for none.
    [[nodiscard]] uint64_t totalMs(uint64_t sessionMs) const;
    [[nodiscard]] std::map<std::string, uint64_t> authorsMs(const std::string& me,
                                                            uint64_t sessionMs) const;

    // A partner's times: an author's larger time is taken, and the total grows by the
    // difference. Mine counts with the session's time, so that this copy's own time coming
    // back adds nothing.
    void merge(const std::map<std::string, uint64_t>& theirsMs,
               const std::string& me,
               uint64_t sessionMs);

private:
    uint64_t m_totalMs = 0;
    std::map<std::string, uint64_t> m_authorsMs;
};

}  // namespace colorizing::document

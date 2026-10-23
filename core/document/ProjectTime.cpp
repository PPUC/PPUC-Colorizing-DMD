#include "document/ProjectTime.h"

#include <algorithm>
#include <utility>

namespace colorizing::document {

void ProjectTime::open(uint64_t totalMs, std::map<std::string, uint64_t> authorsMs)
{
    m_authorsMs = std::move(authorsMs);
    // A total below the authors' sum (a file edited by hand) counts their sum.
    uint64_t sum = 0;
    for (const auto& [author, ms] : m_authorsMs) {
        sum += ms;
    }
    m_totalMs = std::max(totalMs, sum);
}

uint64_t ProjectTime::totalMs(uint64_t sessionMs) const
{
    return m_totalMs + sessionMs;
}

std::map<std::string, uint64_t> ProjectTime::authorsMs(const std::string& me,
                                                       uint64_t sessionMs) const
{
    std::map<std::string, uint64_t> authors = m_authorsMs;
    if (!me.empty() && sessionMs > 0) {
        authors[me] += sessionMs;
    }
    return authors;
}

void ProjectTime::merge(const std::map<std::string, uint64_t>& theirsMs,
                        const std::string& me,
                        uint64_t sessionMs)
{
    for (const auto& [author, theirs] : theirsMs) {
        if (author.empty()) {
            continue;
        }
        const uint64_t session = author == me ? sessionMs : 0;
        uint64_t& mine = m_authorsMs[author];
        if (theirs > mine + session) {
            m_totalMs += theirs - (mine + session);
            mine = theirs - session;
        }
    }
}

}  // namespace colorizing::document

#pragma once

#include <algorithm>
#include <cstdint>

namespace colorizing::app {

// Working time: it counts from each activity (input to the editor) up to idleLimitMs after
// it, so a pause longer than that is not counted beyond its first idleLimitMs. Times are
// milliseconds of one steady clock, given by the caller.
class ActivityClock {
public:
    static constexpr int64_t kDefaultIdleLimitMs = 60000;

    explicit ActivityClock(int64_t idleLimitMs = kDefaultIdleLimitMs)
        : m_idleLimitMs(idleLimitMs)
    {
    }

    // From zero, active at nowMs.
    void start(int64_t nowMs)
    {
        m_countedMs = 0;
        m_lastActivityMs = nowMs;
        m_started = true;
    }

    void activity(int64_t nowMs)
    {
        if (!m_started) {
            start(nowMs);
            return;
        }
        m_countedMs += span(nowMs);
        m_lastActivityMs = std::max(m_lastActivityMs, nowMs);
    }

    [[nodiscard]] int64_t elapsedMs(int64_t nowMs) const
    {
        return m_started ? m_countedMs + span(nowMs) : 0;
    }

    // No activity for longer than the limit: the clock stands still.
    [[nodiscard]] bool isPaused(int64_t nowMs) const
    {
        return m_started && nowMs - m_lastActivityMs > m_idleLimitMs;
    }

private:
    [[nodiscard]] int64_t span(int64_t nowMs) const
    {
        return std::clamp<int64_t>(nowMs - m_lastActivityMs, 0, m_idleLimitMs);
    }

    int64_t m_idleLimitMs;
    int64_t m_countedMs = 0;
    int64_t m_lastActivityMs = 0;
    bool m_started = false;
};

}  // namespace colorizing::app

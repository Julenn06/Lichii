// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <bit>
#include <limits>
#include <map>
#include <mutex>
#include <set>

#include "common/common_types.h"

namespace Common::Log {

/// Counts one occurrence of a repeating message. Returns the count when the message should be
/// logged (the first `burst` times, then at powers of two) and 0 otherwise, so a message hit
/// every draw stays visible without flooding the log.
inline u64 CountRepeat(std::atomic<u64>& counter, u64 burst = 4) {
    const u64 count = counter.fetch_add(1, std::memory_order_relaxed) + 1;
    return count <= burst || std::has_single_bit(count) ? count : 0;
}

/// The keys a message was already logged for, safe to share between threads and capped at
/// `limit` keys so a message keyed by guest data cannot grow without bound. Meant for a
/// function-local static.
template <typename Key>
class LoggedKeys {
public:
    explicit LoggedKeys(size_t limit_ = std::numeric_limits<size_t>::max()) : limit{limit_} {}

    /// True the first time `key` is seen while there is room for it.
    bool First(const Key& key) {
        std::scoped_lock lock{mutex};
        return seen.size() < limit && seen.insert(key).second;
    }

    /// Counts one occurrence of `key`: true for its first `times` occurrences (while there is
    /// room for new keys).
    bool Within(const Key& key, u32 times) {
        std::scoped_lock lock{mutex};
        const auto it = counts.find(key);
        if (it == counts.end()) {
            if (counts.size() >= limit) {
                return false;
            }
            counts.emplace(key, 1);
            return times > 0;
        }
        if (it->second >= times) {
            return false;
        }
        ++it->second;
        return true;
    }

    size_t Size() const {
        std::scoped_lock lock{mutex};
        return seen.size() + counts.size();
    }

private:
    mutable std::mutex mutex;
    std::set<Key> seen;
    std::map<Key, u32> counts;
    size_t limit;
};

} // namespace Common::Log

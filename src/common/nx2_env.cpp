// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "common/logging.h"
#include "common/nx2_env.h"

#ifndef _WIN32
extern char** environ;
#endif

namespace Common::Nx2Env {

namespace {

constexpr KnownSwitch Registry[] = {
#define NX2_ENV(name, kind) KnownSwitch{#name, Kind::kind},
#include "common/nx2_env_registry.inc"
#undef NX2_ENV
};

std::string_view Trim(std::string_view text) {
    constexpr std::string_view blank = " \t\r\n";
    const size_t first = text.find_first_not_of(blank);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(blank) - first + 1);
}

/// Logs each malformed switch once (they are read on hot paths and from many threads).
void ReportMalformed(const char* name, const char* value, const char* expected) {
    static std::mutex mutex;
    static std::set<std::string> reported;
    std::scoped_lock lock{mutex};
    if (reported.emplace(name).second) {
        LOG_WARNING(Common, "Ignoring {}={}: expected {}", name, value, expected);
    }
}

bool LooksNumeric(std::string_view value) {
    // Number switches also take hex addresses, "5f"-style frame counts, decimals and lists:
    // only flag values without a single digit ("yes", "abc").
    return std::any_of(value.begin(), value.end(),
                       [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
}

size_t Distance(std::string_view a, std::string_view b) {
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) {
        row[j] = j;
    }
    for (size_t i = 1; i <= a.size(); ++i) {
        size_t diagonal = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            const size_t above = row[j];
            row[j] = (std::min)({row[j] + 1, row[j - 1] + 1,
                                 diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

} // Anonymous namespace

std::span<const KnownSwitch> KnownSwitches() {
    return Registry;
}

const char* Raw(const char* name) {
    return std::getenv(name);
}

bool Flag(const char* name) {
    const char* const raw = std::getenv(name);
    if (raw == nullptr) {
        return false;
    }
    const std::string_view value = Trim(raw);
    return !value.empty() && value != "0" && value != "false" && value != "off";
}

std::optional<u64> Unsigned(const char* name, int base) {
    const char* const raw = std::getenv(name);
    if (raw == nullptr) {
        return std::nullopt;
    }
    const std::string text{Trim(raw)};
    if (text.empty() || text.front() == '-') {
        ReportMalformed(name, raw, "an unsigned number");
        return std::nullopt;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, base);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE) {
        ReportMalformed(name, raw, "an unsigned number");
        return std::nullopt;
    }
    return static_cast<u64>(value);
}

u64 UnsignedOr(const char* name, u64 fallback, int base) {
    return Unsigned(name, base).value_or(fallback);
}

s64 Signed(const char* name, s64 fallback) {
    const char* const raw = std::getenv(name);
    if (raw == nullptr) {
        return fallback;
    }
    const std::string text{Trim(raw)};
    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (text.empty() || end == text.c_str() || *end != '\0' || errno == ERANGE) {
        ReportMalformed(name, raw, "a whole number");
        return fallback;
    }
    return static_cast<s64>(value);
}

std::optional<double> Decimal(const char* name) {
    const char* const raw = std::getenv(name);
    if (raw == nullptr) {
        return std::nullopt;
    }
    const std::string text{Trim(raw)};
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (text.empty() || end == text.c_str() || *end != '\0' || errno == ERANGE) {
        ReportMalformed(name, raw, "a number");
        return std::nullopt;
    }
    return value;
}

void ValidateEnvironment() {
#ifdef _WIN32
    char** const variables = _environ;
#else
    char** const variables = environ;
#endif
    if (variables == nullptr) {
        return;
    }
    size_t active = 0;
    for (char** entry = variables; *entry != nullptr; ++entry) {
        const std::string_view line{*entry};
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos || !line.starts_with("EDEN_")) {
            continue;
        }
        const std::string_view name = line.substr(0, eq);
        const std::string_view value = line.substr(eq + 1);
        const auto known = std::find_if(std::begin(Registry), std::end(Registry),
                                        [name](const KnownSwitch& item) { return item.name == name; });
        if (known == std::end(Registry)) {
            const auto closest = std::min_element(
                std::begin(Registry), std::end(Registry),
                [name](const KnownSwitch& a, const KnownSwitch& b) {
                    return Distance(name, a.name) < Distance(name, b.name);
                });
            if (closest != std::end(Registry) && Distance(name, closest->name) <= 3) {
                LOG_WARNING(Common, "Unknown switch {} (did you mean {}?); it has no effect",
                            name, closest->name);
            } else {
                LOG_WARNING(Common, "Unknown switch {}; it has no effect", name);
            }
            continue;
        }
        ++active;
        if (known->kind == Kind::Number && !LooksNumeric(Trim(value))) {
            LOG_WARNING(Common, "Switch {}={} is not a number; it will be ignored or misread",
                        name, value);
        }
    }
    LOG_INFO(Common, "{} environment switches active", active);
}

} // namespace Common::Nx2Env

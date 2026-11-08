// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <cstdlib>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "common/log_repeat.h"
#include "common/nx2_env.h"

namespace {

void Set(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void Unset(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

constexpr const char* Name = "EDEN_NX2_TEST_ONLY_SWITCH";

} // Anonymous namespace

TEST_CASE("Nx2Env reads well-formed numbers", "[common][nx2_env]") {
    Set(Name, "1800");
    CHECK(Common::Nx2Env::Unsigned(Name) == 1800u);
    CHECK(Common::Nx2Env::UnsignedOr(Name, 7) == 1800u);
    Set(Name, " 0x40 ");
    CHECK(Common::Nx2Env::Unsigned(Name, 16) == 0x40u);
    CHECK(Common::Nx2Env::Unsigned(Name, 0) == 0x40u);
    Set(Name, "-1");
    CHECK(Common::Nx2Env::Signed(Name, 50) == -1);
    Unset(Name);
}

TEST_CASE("Nx2Env falls back on malformed numbers instead of reading 0",
          "[common][nx2_env]") {
    // strtoull read all of these as 0 (or 1 Hz vsync, an empty pipeline cache...).
    for (const char* value : {"abc", "12abc", "", "-5", "1e3"}) {
        Set(Name, value);
        CHECK(Common::Nx2Env::UnsignedOr(Name, 8000) == 8000u);
        CHECK(Common::Nx2Env::Signed(Name, 60) == (std::string_view{value} == "-5" ? -5 : 60));
    }
    Unset(Name);
    CHECK_FALSE(Common::Nx2Env::Unsigned(Name).has_value());
    CHECK(Common::Nx2Env::UnsignedOr(Name, 256) == 256u);
}

TEST_CASE("Nx2Env flags treat 0, false and off as unset", "[common][nx2_env]") {
    for (const char* value : {"0", "false", "off", ""}) {
        Set(Name, value);
        CHECK_FALSE(Common::Nx2Env::Flag(Name));
    }
    Set(Name, "1");
    CHECK(Common::Nx2Env::Flag(Name));
    Unset(Name);
    CHECK_FALSE(Common::Nx2Env::Flag(Name));
}

TEST_CASE("Nx2Env registry lists the switches the sources read", "[common][nx2_env]") {
    bool split = false;
    bool vsync_is_number = false;
    for (const auto& known : Common::Nx2Env::KnownSwitches()) {
        split |= known.name == "EDEN_NX2_SPLIT";
        if (known.name == "EDEN_NX2_VSYNC_HZ") {
            vsync_is_number = known.kind == Common::Nx2Env::Kind::Number;
        }
    }
    CHECK(split);
    CHECK(vsync_is_number);
}

TEST_CASE("Repeated log messages are kept at bursts and powers of two", "[common][log]") {
    std::atomic<u64> counter{};
    u64 logged = 0;
    for (int i = 0; i < 1000; ++i) {
        if (Common::Log::CountRepeat(counter) != 0) {
            ++logged;
        }
    }
    // 1..4, then 8, 16, ..., 512.
    CHECK(logged == 4 + 7);
    CHECK(counter.load() == 1000u);
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "common/common_types.h"

// Typed, validated access to the EDEN_* / EDEN_NX2_* environment switches
// (dist/nx2_switches.txt). A malformed value is reported once and treated as unset, rather than
// silently read as 0 by strtoull.
namespace Common::Nx2Env {

enum class Kind : u8 {
    Number, ///< Integer (decimal or 0x hex) or decimal value.
    Text,   ///< Anything: a flag, a path, a list of hashes.
};

struct KnownSwitch {
    std::string_view name;
    Kind kind;
};

/// Every switch read anywhere in the sources (nx2_env_registry.inc).
std::span<const KnownSwitch> KnownSwitches();

/// The raw value, or nullptr when unset.
const char* Raw(const char* name);

/// True when set to anything other than "", "0", "false" or "off".
bool Flag(const char* name);

/// The value parsed as an unsigned integer in `base` (0 accepts a 0x prefix). Unset or
/// malformed (reported once) gives std::nullopt.
std::optional<u64> Unsigned(const char* name, int base = 10);

/// Unsigned(name, base), or `fallback` when unset or malformed. (Not an overload of Unsigned:
/// Unsigned(name, 256) would pick the base parameter.)
u64 UnsignedOr(const char* name, u64 fallback, int base = 10);

/// The value parsed as a signed decimal integer, or `fallback` when unset or malformed (reported
/// once).
s64 Signed(const char* name, s64 fallback);

/// The value parsed as a decimal number. Unset or malformed (reported once) gives std::nullopt.
std::optional<double> Decimal(const char* name);

/// Logs every EDEN_* switch in the environment that no code reads (a typo, with the closest
/// known name) and every Number switch whose value is not a number. Call once logging is up.
void ValidateEnvironment();

} // namespace Common::Nx2Env

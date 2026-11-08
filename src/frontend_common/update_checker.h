// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include "common/common_types.h"
#include "common/net/net.h"

namespace UpdateChecker {

/// File names of this fork's packages: Lichii-Frizti-v3-mejorado-v3.6.0-windows-x64.zip.
inline constexpr std::string_view PackagePrefix = "Lichii-Frizti-v3-mejorado-";
inline constexpr std::string_view WindowsPackageSuffix = "-windows-x64.zip";

using Version = std::array<u32, 3>;

/// "3.6.0" or "v3.6.0" -> {3, 6, 0}; anything else (extra parts, signs, junk) -> nullopt.
std::optional<Version> ParseVersion(std::string_view text);

/// The newest non-prerelease vX.Y.Z release newer than `current_version` that carries a package
/// ending in `package_suffix`.
std::optional<Common::Net::Release> SelectVersionUpdate(
    std::span<const Common::Net::Release> releases, std::string_view current_version,
    std::string_view package_suffix);

/// The package to install from a release chosen by SelectVersionUpdate (nullopt otherwise).
std::optional<Common::Net::Asset> GetPackage(const Common::Net::Release& release);

enum class CheckStatus {
    UpdateAvailable,
    UpToDate,
    NetworkError, ///< GitHub could not be reached or answered something unusable.
    Unsupported,  ///< This build has no version or platform to compare with.
};

struct CheckResult {
    CheckStatus status = CheckStatus::Unsupported;
    std::optional<Common::Net::Release> release;
};

/// This build's version ("3.6.0"), empty when it was built without dist/mejorado_version.txt.
std::string_view CurrentVersion();

/// Blocking: asks GitHub for the releases and compares them with this build.
CheckResult CheckForUpdate();

std::optional<Common::Net::Release> GetUpdate();
std::optional<Common::Net::Release> SelectUpdate(std::span<const Common::Net::Release> releases,
                                                 std::string_view build_tag,
                                                 std::string_view platform);

} // namespace UpdateChecker

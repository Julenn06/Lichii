// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <charconv>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include "common/logging.h"
#include "common/scm_rev.h"
#include "frontend_common/update_checker.h"

namespace UpdateChecker {

namespace {

std::optional<u64> ParseBuildNumber(std::string_view tag) {
    constexpr std::string_view prefix = "build-";
    if (!tag.starts_with(prefix)) {
        return std::nullopt;
    }
    tag.remove_prefix(prefix.size());
    u64 number{};
    const auto [end, error] = std::from_chars(tag.data(), tag.data() + tag.size(), number);
    if (error != std::errc{} || end != tag.data() + tag.size() || number == 0) {
        return std::nullopt;
    }
    return number;
}

std::string_view PackageSuffix() {
#ifdef _WIN32
    return WindowsPackageSuffix;
#else
    return {};
#endif
}

} // namespace

std::optional<Version> ParseVersion(std::string_view text) {
    if (text.starts_with('v')) {
        text.remove_prefix(1);
    }
    Version version{};
    for (std::size_t i = 0; i < version.size(); ++i) {
        if (text.empty() || text.front() < '0' || text.front() > '9') {
            return std::nullopt;
        }
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(),
                                                  version[i]);
        if (error != std::errc{}) {
            return std::nullopt;
        }
        text.remove_prefix(static_cast<std::size_t>(end - text.data()));
        if (i + 1 < version.size()) {
            if (!text.starts_with('.')) {
                return std::nullopt;
            }
            text.remove_prefix(1);
        }
    }
    if (!text.empty()) {
        return std::nullopt;
    }
    return version;
}

std::optional<Common::Net::Release> SelectVersionUpdate(
    std::span<const Common::Net::Release> releases, std::string_view current_version,
    std::string_view package_suffix) {
    auto newest = ParseVersion(current_version);
    if (!newest || package_suffix.empty()) {
        return std::nullopt;
    }
    const Common::Net::Release* selected = nullptr;
    for (const auto& release : releases) {
        if (release.prerelease || !release.tag.starts_with('v')) {
            continue;
        }
        const auto version = ParseVersion(release.tag);
        if (version && *version > *newest &&
            release.FindAsset(PackagePrefix, package_suffix).has_value()) {
            newest = version;
            selected = &release;
        }
    }
    return selected ? std::make_optional(*selected) : std::nullopt;
}

std::optional<Common::Net::Asset> GetPackage(const Common::Net::Release& release) {
    const auto suffix = PackageSuffix();
    if (suffix.empty()) {
        return std::nullopt;
    }
    return release.FindAsset(PackagePrefix, suffix);
}

std::string_view CurrentVersion() {
    return Common::g_mejorado_version;
}

CheckResult CheckForUpdate() {
    if (!ParseVersion(CurrentVersion()) || PackageSuffix().empty()) {
        return {.status = CheckStatus::Unsupported};
    }
    const auto body = Common::Net::GetReleasesBody();
    if (!body) {
        return {.status = CheckStatus::NetworkError};
    }
    // An error object ("API rate limit exceeded") or broken JSON is not "no new release".
    const auto json = nlohmann::json::parse(*body, nullptr, false);
    if (!json.is_array()) {
        LOG_WARNING(Frontend, "Unexpected answer from the releases API");
        return {.status = CheckStatus::NetworkError};
    }
    const auto releases = Common::Net::Release::ListFromJson(
        json, fmt::format("https://{}", Common::g_build_auto_update_stable_api),
        Common::g_build_auto_update_stable_repo);
    auto update = SelectVersionUpdate(releases, CurrentVersion(), PackageSuffix());
    if (!update) {
        return {.status = CheckStatus::UpToDate};
    }
    LOG_INFO(Frontend, "Update available: {} (running {})", update->tag, CurrentVersion());
    return {.status = CheckStatus::UpdateAvailable, .release = std::move(update)};
}

std::optional<Common::Net::Release> SelectUpdate(std::span<const Common::Net::Release> releases,
                                                 std::string_view build_tag,
                                                 std::string_view platform) {
    auto newest = ParseBuildNumber(build_tag);
    if (!newest) {
        return std::nullopt;
    }

    const Common::Net::Release* selected = nullptr;
    for (const auto& release : releases) {
        const auto number = ParseBuildNumber(release.tag);
        if (number && *number > *newest && !release.GetPlatformAssets(platform).empty()) {
            newest = number;
            selected = &release;
        }
    }
    return selected ? std::make_optional(*selected) : std::nullopt;
}

std::optional<Common::Net::Release> GetUpdate() {
    // This fork's builds carry a vX.Y.Z version; compare with its releases.
    if (ParseVersion(CurrentVersion())) {
        return CheckForUpdate().release;
    }

    if (!ParseBuildNumber(Common::g_build_release_tag) ||
        std::string_view{Common::g_build_release_platform}.empty()) {
        return std::nullopt;
    }

    auto update = SelectUpdate(Common::Net::GetReleases(), Common::g_build_release_tag,
                               Common::g_build_release_platform);
    if (update) {
        LOG_INFO(Frontend, "Received update {}", update->title);
    }
    return update;
}

} // namespace UpdateChecker

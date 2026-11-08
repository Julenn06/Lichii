// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include "common/common_types.h"

namespace Common::Net {

struct Asset {
    std::string name;
    std::string url;
    std::string path;
    std::string filename;
    /// GitHub's "sha256:<hex>" digest of the file, or empty when the API did not report one.
    std::string digest;
};

struct Release {
    std::string title;
    std::string body;
    std::string tag;
    std::string base_download_url;
    std::string html_url;
    std::string host;

    std::vector<std::string> assets;
    /// The "digest" of each entry of `assets` (same order), empty where GitHub gave none.
    std::vector<std::string> asset_digests;

    u64 id;
    u64 published;
    bool prerelease;

    // Get the relevant list of assets for the current platform.
    std::vector<Asset> GetPlatformAssets() const;
    std::vector<Asset> GetPlatformAssets(std::string_view platform) const;

    // The asset of this release whose file name starts with `prefix` and ends with `suffix`
    // (only github.com downloads of this release's own tag count).
    std::optional<Asset> FindAsset(std::string_view prefix, std::string_view suffix) const;

    static std::optional<Release> FromJson(const nlohmann::json& json, const std::string &host, const std::string& repo);
    static std::optional<Release> FromJson(const std::string_view& json, const std::string &host, const std::string& repo);
    static std::vector<Release> ListFromJson(const nlohmann::json &json, const std::string &host, const std::string &repo);
    static std::vector<Release> ListFromJson(const std::string_view &json, const std::string &host, const std::string &repo);
};

// Make a request via httplib, and return the response body if applicable.
std::optional<std::string> MakeRequest(const std::string &url, const std::string &path);

struct DownloadResult {
    bool ok = false;
    int status = 0;    ///< Last HTTP status, 0 when no response arrived.
    std::string error; ///< Transport error, empty when the server answered.
};

// Download `url` + `path` (e.g. "https://github.com", "/owner/repo/releases/download/...") into
// `receiver`, following redirects to other hosts such as GitHub's asset storage. `progress`
// returning false cancels the download.
DownloadResult Download(std::string url, std::string path,
                        const std::function<bool(const char*, std::size_t)>& receiver,
                        const std::function<bool(u64 current, u64 total)>& progress);

// Get the latest releases, including prereleases.
std::vector<Release> GetReleases();

// Get the latest releases as text.
std::optional<std::string> GetReleasesBody();

}

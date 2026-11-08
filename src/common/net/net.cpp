// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>
#include <boost/algorithm/string/replace.hpp>

#include <fmt/format.h>
#include "common/scm_rev.h"
#include "net.h"

#include "common/logging.h"

#include "common/httplib.h"

#ifdef YUZU_BUNDLED_OPENSSL
#include <openssl/cert.h>
#endif

namespace Common::Net {

std::vector<Asset> Release::GetPlatformAssets() const {
    return GetPlatformAssets(Common::g_build_release_platform);
}

std::vector<Asset> Release::GetPlatformAssets(std::string_view platform) const {
    std::vector<std::string_view> extensions;
    if (platform == "linux-x86_64" || platform == "linux-x86_64-pgo" ||
        platform == "linux-aarch64" || platform == "linux-aarch64-pgo") {
        extensions = {".AppImage", ".tar.gz"};
    } else if (platform == "windows-x86_64" || platform == "windows-x86_64-pgo" ||
               platform == "macos-arm64") {
        extensions = {".zip"};
    } else {
        return {};
    }

    const std::string host_url = "https://github.com";
    const auto path = base_download_url + "/";
    for (const auto extension : extensions) {
        const auto filename = fmt::format("lichii-{}{}", platform, extension);
        for (const auto& asset : assets) {
            if (asset == host_url + path + filename) {
                return {{.name = platform.ends_with("-pgo") ? "PGO" : "Standard",
                         .url = host_url,
                         .path = path + filename,
                         .filename = filename}};
            }
        }
    }
    return {};
}

std::optional<Asset> Release::FindAsset(std::string_view prefix, std::string_view suffix) const {
    constexpr std::string_view host_url = "https://github.com";
    const std::string path = base_download_url + "/";
    for (std::size_t i = 0; i < assets.size(); ++i) {
        const std::string_view asset = assets[i];
        if (!asset.starts_with(host_url)) {
            continue;
        }
        const std::string_view rest = asset.substr(host_url.size());
        if (!rest.starts_with(path)) {
            continue;
        }
        const std::string_view filename = rest.substr(path.size());
        if (filename.size() <= prefix.size() + suffix.size() ||
            filename.find('/') != std::string_view::npos || !filename.starts_with(prefix) ||
            !filename.ends_with(suffix)) {
            continue;
        }
        return Asset{.name = "Standard",
                     .url = std::string{host_url},
                     .path = std::string{rest},
                     .filename = std::string{filename},
                     .digest = i < asset_digests.size() ? asset_digests[i] : std::string{}};
    }
    return std::nullopt;
}

static inline u64 ParseIsoTimestamp(const std::string& iso) {
    if (iso.empty())
        return 0;

    std::string buf = iso;
    if (buf.back() == 'Z')
        buf.pop_back();

    std::tm tm{};
    std::istringstream ss(buf);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail())
        return 0;

#ifdef _WIN32
    return static_cast<u64>(_mkgmtime(&tm));
#else
    return static_cast<u64>(timegm(&tm));
#endif
}

std::optional<Release> Release::FromJson(const nlohmann::json& json, const std::string& host,
                                         const std::string& repo) {
    Release rel;
    if (!json.is_object())
        return std::nullopt;
    if (json.value("draft", false))
        return std::nullopt;

    rel.tag = json.value("tag_name", std::string{});
    if (rel.tag.empty())
        return std::nullopt;

    rel.title = json.contains("name") && json["name"].is_string()
                    ? json["name"].get<std::string>() : rel.tag;
    rel.id = json.value("id", std::hash<std::string>{}(rel.title));

    rel.published = ParseIsoTimestamp(json.value("published_at", std::string{}));
    rel.prerelease = json.value("prerelease", false);

    auto body = json.contains("body") && json["body"].is_string()
                    ? json["body"].get<std::string>() : rel.title;
    boost::replace_all(body, "\\r", "");
    boost::replace_all(body, "\\n", "\n");
    rel.body = body;

    rel.host = host;

    const auto release_base =
        fmt::format("{}/{}/releases", Common::g_build_auto_update_website, repo);
    const auto fallback_html = fmt::format("{}/tag/{}", release_base, rel.tag);
    rel.html_url = json.value("html_url", fallback_html);

    rel.base_download_url = fmt::format("/{}/releases/download/{}", repo, rel.tag);
    const auto assets = json.value("assets", nlohmann::json::array());
    if (!assets.is_array()) {
        return std::nullopt;
    }
    for (const auto& asset : assets) {
        if (!asset.is_object()) {
            continue;
        }
        const auto url = asset.find("browser_download_url");
        const auto digest = asset.find("digest");
        rel.assets.emplace_back(url != asset.end() && url->is_string() ? url->get<std::string>()
                                                                       : std::string{});
        rel.asset_digests.emplace_back(digest != asset.end() && digest->is_string()
                                           ? digest->get<std::string>()
                                           : std::string{});
    }

    return rel;
}

std::optional<Release> Release::FromJson(const std::string_view& json, const std::string& host,
                                         const std::string& repo) {
    try {
        return FromJson(nlohmann::json::parse(json), host, repo);
    } catch (std::exception& e) {
        LOG_WARNING(Common, "Failed to parse JSON: {}", e.what());
    }

    return {};
}

std::vector<Release> Release::ListFromJson(const nlohmann::json& json, const std::string& host,
                                           const std::string& repo) {
    if (!json.is_array())
        return {};

    std::vector<Release> releases;
    for (const auto& obj : json) {
        auto rel = Release::FromJson(obj, host, repo);
        if (rel)
            releases.emplace_back(rel.value());
    }
    return releases;
}

std::vector<Release> Release::ListFromJson(const std::string_view& json, const std::string& host,
                                           const std::string& repo) {
    try {
        return ListFromJson(nlohmann::json::parse(json), host, repo);
    } catch (std::exception& e) {
        LOG_WARNING(Common, "Failed to parse JSON: {}", e.what());
    }

    return {};
}

std::optional<std::string> MakeRequest(const std::string& url, const std::string& path) {
    try {
        constexpr std::size_t timeout_seconds = 15;

        std::unique_ptr<httplib::Client> client = std::make_unique<httplib::Client>(url);
        client->set_connection_timeout(timeout_seconds);
        client->set_read_timeout(timeout_seconds);
        client->set_write_timeout(timeout_seconds);

#ifdef YUZU_BUNDLED_OPENSSL
        client->load_ca_cert_store(kCert, sizeof(kCert));
#endif

        if (client == nullptr) {
            LOG_ERROR(Common, "Invalid URL {}{}", url, path);
            return {};
        }

        httplib::Request request{
            .method = "GET",
            .path = path,
            .headers = {{"Accept", "application/vnd.github+json"}, {"User-Agent", "Lichii"}},
        };

        client->set_follow_location(true);
        httplib::Result result = client->send(request);

        if (!result) {
            LOG_ERROR(Common, "GET to {}{} returned null", url, path);
            return {};
        }

        const auto& response = result.value();
        if (response.status >= 400) {
            LOG_ERROR(Common, "GET to {}{} returned error status code: {}", url, path,
                      response.status);
            return {};
        }
        if (!response.has_header("content-type")) {
            LOG_ERROR(Common, "GET to {}{} returned no content", url, path);
            return {};
        }

        return response.body;
    } catch (std::exception& e) {
        LOG_ERROR(Common, "GET to {}{} failed during update check: {}", url, path, e.what());
        return std::nullopt;
    }
}

DownloadResult Download(std::string url, std::string path,
                        const std::function<bool(const char*, std::size_t)>& receiver,
                        const std::function<bool(u64 current, u64 total)>& progress) {
    DownloadResult out;
    // github.com answers with a redirect to a signed URL on another host; httplib would build
    // that second client without our CA store, so the redirects are followed here.
    for (int hop = 0; hop < 6; ++hop) {
        try {
            httplib::Client client(url);
            if (!client.is_valid()) {
                out.error = "invalid URL " + url;
                return out;
            }
            client.set_connection_timeout(15);
            client.set_read_timeout(30);
            client.set_write_timeout(15);
            client.set_follow_location(false);
#ifdef YUZU_BUNDLED_OPENSSL
            client.load_ca_cert_store(kCert, sizeof(kCert));
#endif
            const httplib::Headers headers{{"User-Agent", "Lichii"},
                                           {"Accept", "application/octet-stream"}};
            int status = 0;
            std::string location;
            const auto result = client.Get(
                path, headers,
                [&](const httplib::Response& response) {
                    status = response.status;
                    if (status >= 300 && status < 400) {
                        location = response.get_header_value("Location");
                        return false;
                    }
                    return status >= 200 && status < 300;
                },
                [&](const char* data, std::size_t length) { return receiver(data, length); },
                [&](std::size_t current, std::size_t total) {
                    return !progress || progress(current, total);
                });
            out.status = status;
            if (status >= 300 && status < 400) {
                if (location.empty()) {
                    out.error = "redirect without a location";
                    return out;
                }
                if (location.starts_with('/')) {
                    path = location;
                    continue;
                }
                const auto scheme = location.find("://");
                if (scheme == std::string::npos) {
                    out.error = "unsupported redirect " + location;
                    return out;
                }
                const auto slash = location.find('/', scheme + 3);
                url = location.substr(0, slash);
                path = slash == std::string::npos ? std::string{"/"} : location.substr(slash);
                continue;
            }
            out.ok = result && status >= 200 && status < 300;
            if (!result && status == 0) {
                out.error = httplib::to_string(result.error());
            }
            if (!out.ok) {
                LOG_ERROR(Common, "Download of {}{} failed: status {} {}", url, path, status,
                          out.error);
            }
            return out;
        } catch (const std::exception& e) {
            out.error = e.what();
            LOG_ERROR(Common, "Download of {}{} failed: {}", url, path, e.what());
            return out;
        }
    }
    out.error = "too many redirects";
    return out;
}

std::vector<Release> GetReleases() {
    const auto body = GetReleasesBody();

    if (!body) {
        LOG_WARNING(Common, "Failed to get Lichii releases");
        return {};
    }

    const std::string_view body_str = body.value();
    const auto url = fmt::format("https://{}", Common::g_build_auto_update_stable_api);
    return Release::ListFromJson(body_str, url, Common::g_build_auto_update_stable_repo);
}

std::optional<std::string> GetReleasesBody() {
    const auto releases_path = Common::g_build_auto_update_api_path;
    const auto url = fmt::format("https://{}", Common::g_build_auto_update_stable_api);

    return MakeRequest(url, releases_path);
}

} // namespace Common::Net

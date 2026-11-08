// SPDX-FileCopyrightText: Copyright 2026 Lichii Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "common/httplib.h"
#include "common/net/net.h"
#include "frontend_common/update_checker.h"

namespace {

Common::Net::Release MakeRelease(std::string tag, std::string filename) {
    const nlohmann::json json{
        {"tag_name", tag},
        {"name", "Lichii nx2 " + tag},
        {"prerelease", true},
        {"assets",
         {{{"browser_download_url",
            "https://github.com/zortos293/Lichii/releases/download/" + tag + "/" + filename}}}},
    };
    return Common::Net::Release::FromJson(json, "https://api.github.com", "zortos293/Lichii")
        .value();
}

} // namespace

TEST_CASE("Lichii updates compare build numbers, not tag strings", "[updater]") {
    const auto releases = std::array{
        MakeRelease("build-9", "lichii-windows-x86_64.zip"),
        MakeRelease("build-100", "lichii-windows-x86_64.zip"),
        MakeRelease("build-99", "lichii-windows-x86_64.zip"),
    };
    const auto update = UpdateChecker::SelectUpdate(releases, "build-70", "windows-x86_64");
    REQUIRE(update);
    CHECK(update->tag == "build-100");
    CHECK_FALSE(UpdateChecker::SelectUpdate(releases, "build-100", "windows-x86_64"));
    CHECK_FALSE(UpdateChecker::SelectUpdate(releases, "build-101", "windows-x86_64"));
    CHECK_FALSE(UpdateChecker::SelectUpdate({}, "build-70", "windows-x86_64"));
}

TEST_CASE("Lichii ignores dev builds and malformed release tags", "[updater]") {
    const std::string tag =
        GENERATE("", "nx2", "v0.2.0", "abc123-nx2", "nx2-build-71", "build-", "build-0", "build--1",
                 "build-+71", "build-71junk", "build- 71", "build-18446744073709551616");
    const auto releases = std::array{MakeRelease("build-71", "lichii-windows-x86_64.zip")};
    CHECK_FALSE(UpdateChecker::SelectUpdate(releases, tag, "windows-x86_64"));
    auto invalid = releases;
    invalid[0].tag = tag;
    invalid[0].assets = {"https://github.com/zortos293/Lichii/releases/download/" + tag +
                         "/lichii-windows-x86_64.zip"};
    CHECK_FALSE(UpdateChecker::SelectUpdate(invalid, "build-70", "windows-x86_64"));
}

TEST_CASE("Lichii selects only the matching platform and PGO variant", "[updater]") {
    const std::string platform =
        GENERATE("windows-x86_64", "windows-x86_64-pgo", "linux-x86_64", "linux-x86_64-pgo",
                 "linux-aarch64", "linux-aarch64-pgo", "macos-arm64");
    auto release = MakeRelease("build-71", "unrelated.zip");
    for (const auto name :
         {"windows-x86_64.zip", "windows-x86_64-pgo.zip", "linux-x86_64.tar.gz",
          "linux-x86_64-pgo.tar.gz", "linux-aarch64.tar.gz", "linux-aarch64-pgo.tar.gz",
          "macos-arm64.zip", "linux-x86_64.AppImage", "linux-x86_64-pgo.AppImage",
          "linux-aarch64.AppImage", "linux-aarch64-pgo.AppImage"}) {
        release.assets.push_back(
            "https://github.com/zortos293/Lichii/releases/download/build-71/lichii-" +
            std::string{name});
    }
    const auto selected = release.GetPlatformAssets(platform);
    REQUIRE(selected.size() == 1);
    CHECK(selected[0].filename ==
          "lichii-" + platform + (platform.starts_with("linux-") ? ".AppImage" : ".zip"));
    CHECK(selected[0].url == "https://github.com");
    CHECK(selected[0].path ==
          "/zortos293/Lichii/releases/download/build-71/" + selected[0].filename);
    CHECK_FALSE(release.GetPlatformAssets("android-arm64").size());
    CHECK_FALSE(release.GetPlatformAssets("").size());
}

TEST_CASE("Lichii skips releases without its asset and falls back to Linux archives", "[updater]") {
    const auto releases = std::array{
        MakeRelease("build-73", "lichii-linux-x86_64.AppImage"),
        MakeRelease("build-71", "lichii-linux-x86_64-pgo.tar.gz"),
        MakeRelease("build-72", "lichii-linux-aarch64-pgo.AppImage"),
    };
    const auto update = UpdateChecker::SelectUpdate(releases, "build-70", "linux-x86_64-pgo");
    REQUIRE(update);
    CHECK(update->tag == "build-71");
    CHECK(update->GetPlatformAssets("linux-x86_64-pgo")[0].filename ==
          "lichii-linux-x86_64-pgo.tar.gz");
    CHECK_FALSE(UpdateChecker::SelectUpdate(releases, "build-70", "windows-x86_64"));
}

TEST_CASE("Lichii does not select Eden or lookalike download URLs", "[updater]") {
    const std::string url = GENERATE(
        "https://github.com/eden-emu/eden/releases/download/build-71/lichii-windows-x86_64.zip",
        "https://github.com/zortos293/Lichii/releases/download/build-70/lichii-windows-x86_64.zip",
        "https://github.com/zortos293/Lichii/releases/download/build-71/"
        "not-lichii-windows-x86_64.zip",
        "https://github.com.example.com/zortos293/Lichii/releases/download/build-71/"
        "lichii-windows-x86_64.zip");
    auto release = MakeRelease("build-71", "lichii-windows-x86_64.zip");
    release.assets = {url};
    CHECK(release.GetPlatformAssets("windows-x86_64").empty());
}

TEST_CASE("GitHub failures and invalid JSON produce no releases", "[updater]") {
    const std::string_view json =
        GENERATE("", "not JSON", R"({"message":"API rate limit exceeded"})", "null", "[]",
                 R"([{"tag_name":42}])", R"([{"tag_name":"build-71","assets":null}])",
                 R"([{"tag_name":"build-71","draft":true}])");
    CHECK(Common::Net::Release::ListFromJson(json, "https://api.github.com", "zortos293/Lichii")
              .empty());
}

TEST_CASE("GitHub optional fields can be null or missing", "[updater]") {
    const std::string_view json = R"([{"tag_name":"build-71","name":null,"body":null}])";
    const auto releases =
        Common::Net::Release::ListFromJson(json, "https://api.github.com", "zortos293/Lichii");
    REQUIRE(releases.size() == 1);
    CHECK(releases[0].title == "build-71");
    CHECK(releases[0].assets.empty());
    CHECK(releases[0].html_url == "https://github.com/zortos293/Lichii/releases/tag/build-71");
}

TEST_CASE("Release requests handle HTTP failures without throwing", "[updater]") {
    const int status = GENERATE(200, 403, 429, 500);
    httplib::Server server;
    server.Get("/releases", [status](const httplib::Request& request, httplib::Response& response) {
        response.status =
            request.get_header_value("User-Agent") == "Lichii" &&
                    request.get_header_value("Accept") == "application/vnd.github+json"
                ? status
                : 400;
        response.set_content("[]", "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    const std::jthread listener([&server](std::stop_token stop) {
        const std::stop_callback stop_server(stop, [&server] { server.stop(); });
        server.listen_after_bind();
    });
    server.wait_until_ready();
    const auto response =
        Common::Net::MakeRequest("http://127.0.0.1:" + std::to_string(port), "/releases");
    CHECK(response.has_value() == (status == 200));
}

namespace {

Common::Net::Release MakeForkRelease(std::string tag, bool prerelease = false,
                                     std::string filename = {}) {
    if (filename.empty()) {
        filename = "Lichii-Frizti-v3-mejorado-" + tag + "-windows-x64.zip";
    }
    const nlohmann::json json{
        {"tag_name", tag},
        {"name", "Lichii Frizti v3 mejorado " + tag},
        {"prerelease", prerelease},
        {"assets",
         {{{"browser_download_url",
            "https://github.com/Julenn06/Lichii/releases/download/" + tag + "/" + filename},
           {"digest", "sha256:abc123"}}}},
    };
    return Common::Net::Release::FromJson(json, "https://api.github.com", "Julenn06/Lichii")
        .value();
}

} // namespace

TEST_CASE("Fork versions parse only as three plain numbers", "[updater]") {
    using UpdateChecker::ParseVersion;
    CHECK(ParseVersion("3.6.0") == UpdateChecker::Version{3, 6, 0});
    CHECK(ParseVersion("v3.10.2") == UpdateChecker::Version{3, 10, 2});
    const std::string bad = GENERATE("", "v", "3", "3.6", "3.6.0.1", "3.6.x", "v3.6.0-rc1",
                                     "-3.6.0", "+3.6.0", "3..6", " 3.6.0", "3.6.0 ", "vv3.6.0",
                                     "3.6.99999999999");
    CHECK_FALSE(ParseVersion(bad));
}

TEST_CASE("Fork updates pick the newest stable version with a package", "[updater]") {
    const auto releases = std::array{
        MakeForkRelease("v3.5.0"),
        MakeForkRelease("v3.10.0"),
        MakeForkRelease("v3.9.1"),
        MakeForkRelease("v4.0.0", true),
        MakeForkRelease("v3.11.0", false, "Lichii-Frizti-v3-mejorado-v3.11.0-linux.tar.gz"),
        MakeForkRelease("build-200"),
    };
    const auto update = UpdateChecker::SelectVersionUpdate(releases, "3.6.0", "-windows-x64.zip");
    REQUIRE(update);
    CHECK(update->tag == "v3.10.0");
    CHECK_FALSE(UpdateChecker::SelectVersionUpdate(releases, "3.10.0", "-windows-x64.zip"));
    CHECK_FALSE(UpdateChecker::SelectVersionUpdate(releases, "", "-windows-x64.zip"));
    CHECK_FALSE(UpdateChecker::SelectVersionUpdate(releases, "3.6.0", ""));
}

TEST_CASE("Fork packages come only from the release's own github.com download", "[updater]") {
    auto release = MakeForkRelease("v3.7.0");
    const auto asset = release.FindAsset("Lichii-Frizti-v3-mejorado-", "-windows-x64.zip");
    REQUIRE(asset);
    CHECK(asset->url == "https://github.com");
    CHECK(asset->path == "/Julenn06/Lichii/releases/download/v3.7.0/"
                         "Lichii-Frizti-v3-mejorado-v3.7.0-windows-x64.zip");
    CHECK(asset->filename == "Lichii-Frizti-v3-mejorado-v3.7.0-windows-x64.zip");
    CHECK(asset->digest == "sha256:abc123");

    const std::string url = GENERATE(
        "https://github.com/Other/Lichii/releases/download/v3.7.0/"
        "Lichii-Frizti-v3-mejorado-v3.7.0-windows-x64.zip",
        "https://github.com/Julenn06/Lichii/releases/download/v3.6.0/"
        "Lichii-Frizti-v3-mejorado-v3.7.0-windows-x64.zip",
        "https://github.com.example.com/Julenn06/Lichii/releases/download/v3.7.0/"
        "Lichii-Frizti-v3-mejorado-v3.7.0-windows-x64.zip",
        "https://github.com/Julenn06/Lichii/releases/download/v3.7.0/sub/"
        "Lichii-Frizti-v3-mejorado-v3.7.0-windows-x64.zip",
        "https://github.com/Julenn06/Lichii/releases/download/v3.7.0/"
        "Lichii-Frizti-v3-mejorado--windows-x64.zip");
    release.assets = {url};
    CHECK_FALSE(release.FindAsset("Lichii-Frizti-v3-mejorado-", "-windows-x64.zip"));
}

TEST_CASE("Release assets tolerate missing or null digests", "[updater]") {
    const std::string_view json =
        R"([{"tag_name":"v3.7.0","assets":[{"browser_download_url":"https://github.com/a/b/releases/download/v3.7.0/x.zip","digest":null},{"browser_download_url":"https://github.com/a/b/releases/download/v3.7.0/y.zip"},42]}])";
    const auto releases =
        Common::Net::Release::ListFromJson(json, "https://api.github.com", "a/b");
    REQUIRE(releases.size() == 1);
    REQUIRE(releases[0].assets.size() == 2);
    CHECK(releases[0].asset_digests == std::vector<std::string>{"", ""});
}

TEST_CASE("Downloads follow redirects to another host", "[updater]") {
    httplib::Server storage;
    storage.Get("/blob", [](const httplib::Request&, httplib::Response& response) {
        response.set_content("package-bytes", "application/octet-stream");
    });
    const int storage_port = storage.bind_to_any_port("127.0.0.1");
    REQUIRE(storage_port > 0);
    httplib::Server origin;
    origin.Get("/asset", [storage_port](const httplib::Request&, httplib::Response& response) {
        response.set_redirect("http://127.0.0.1:" + std::to_string(storage_port) + "/blob?sig=1");
    });
    origin.Get("/missing", [](const httplib::Request&, httplib::Response& response) {
        response.status = 404;
    });
    const int origin_port = origin.bind_to_any_port("127.0.0.1");
    REQUIRE(origin_port > 0);
    const std::jthread storage_thread([&storage](std::stop_token stop) {
        const std::stop_callback stop_server(stop, [&storage] { storage.stop(); });
        storage.listen_after_bind();
    });
    const std::jthread origin_thread([&origin](std::stop_token stop) {
        const std::stop_callback stop_server(stop, [&origin] { origin.stop(); });
        origin.listen_after_bind();
    });
    storage.wait_until_ready();
    origin.wait_until_ready();

    const std::string origin_url = "http://127.0.0.1:" + std::to_string(origin_port);
    std::string body;
    const auto receiver = [&body](const char* data, std::size_t length) {
        body.append(data, length);
        return true;
    };
    const auto ok = Common::Net::Download(origin_url, "/asset", receiver, nullptr);
    CHECK(ok.ok);
    CHECK(ok.status == 200);
    CHECK(body == "package-bytes");

    body.clear();
    const auto missing = Common::Net::Download(origin_url, "/missing", receiver, nullptr);
    CHECK_FALSE(missing.ok);
    CHECK(missing.status == 404);
    CHECK(body.empty());
}

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2014 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>

namespace Common {

extern const char g_scm_rev[];
extern const char g_scm_branch[];
extern const char g_scm_desc[];
extern const char g_build_name[];
extern const char g_build_date[];
extern const char g_build_fullname[];
extern const char g_build_version[];
extern const char g_build_id[];
extern const char g_build_release_tag[];
extern const char g_build_release_platform[];
/// This fork's release version ("3.6.0", tag v3.6.0 on Julenn06/Lichii); empty when unknown.
extern const char g_mejorado_version[];
extern const char g_title_bar_format_idle[];
extern const char g_title_bar_format_running[];
extern const char g_shader_cache_version[];
extern const char g_compiler_id[];

extern const bool g_is_dev_build;
extern const bool g_is_nightly_build;

extern const char g_build_auto_update_website[];
extern const char g_build_auto_update_api_path[];
extern const char g_build_auto_update_repo[];
extern const char g_build_auto_update_stable_api[];
extern const char g_build_auto_update_stable_repo[];

/// Frizti fork releases ("Frizti v3"): the window title and log show the release number.
inline bool IsFriztiBuild() {
    return std::string_view{g_build_version}.starts_with("Frizti v");
}

} // namespace Common

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace QtCommon::Compress {

/// Zips every file under `dir` into `zip_path` (compress.h without its QuaZip include, for
/// frontends that do not link QuaZip).
bool ZipDirectory(const QString& zip_path, const QString& dir);

} // namespace QtCommon::Compress

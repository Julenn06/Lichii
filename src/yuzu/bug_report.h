// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QWidget;

namespace BugReport {

/// Asks where to save, then zips what a bug report needs: the current and previous log, the
/// global and per-game configuration with account tokens and API keys blanked, and the user's
/// nx2_switches.txt. The user name is replaced in every file and keys are never included.
void Create(QWidget* parent);

} // namespace BugReport

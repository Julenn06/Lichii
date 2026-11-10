// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace CrashReport {

/// Windows: on an unhandled exception, writes lichii-crash-<time>-<pid>.dmp (a minidump with every
/// thread's stack) and a .txt next to it (build, exception, the faulting thread's call stack as
/// module+offset) to the crash dumps directory, then lets the process end as before. Without it a
/// crash only stops the log. Nothing leaves the PC. Keeps the newest few reports. No-op elsewhere.
void Install();

} // namespace CrashReport

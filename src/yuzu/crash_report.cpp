// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "crash_report.h"

#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <system_error>
#include <vector>

#include <windows.h>

#include "common/fs/path_util.h"
#include "common/scm_rev.h"

namespace CrashReport {
namespace {

constexpr size_t KeptReports = 5;
constexpr size_t MaxFrames = 48;

// Filled by Install: the handler must not allocate (the heap may be what broke).
wchar_t g_directory[MAX_PATH];
using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, int, const void*, const void*,
                                          const void*);
MiniDumpWriteDumpFn g_write_dump = nullptr;

struct DumpExceptionInformation {
    DWORD thread_id;
    EXCEPTION_POINTERS* exception_pointers;
    BOOL client_pointers;
};

void Write(HANDLE file, const char* text, int length) {
    if (length > 0) {
        DWORD written{};
        WriteFile(file, text, static_cast<DWORD>(length), &written, nullptr);
    }
}

/// "module.dll+0x1234" for a code address, or the bare address outside every module (JIT code).
void WriteAddress(HANDLE file, const char* label, DWORD64 address) {
    char line[MAX_PATH + 96];
    PVOID base{};
    char module[MAX_PATH];
    if (RtlPcToFileHeader(reinterpret_cast<PVOID>(address), &base) != nullptr &&
        GetModuleFileNameA(static_cast<HMODULE>(base), module, MAX_PATH) != 0) {
        const char* name = module;
        for (const char* c = module; *c != '\0'; ++c) {
            if (*c == '\\' || *c == '/') {
                name = c + 1;
            }
        }
        Write(file, line,
              std::snprintf(line, sizeof(line), "%s %s+0x%llx\r\n", label, name,
                            static_cast<unsigned long long>(address -
                                                            reinterpret_cast<DWORD64>(base))));
    } else {
        Write(file, line,
              std::snprintf(line, sizeof(line), "%s 0x%llx (no module: JIT or data)\r\n", label,
                            static_cast<unsigned long long>(address)));
    }
}

/// One frame up the stack; false when there is none or the stack cannot be read (a crash can
/// leave it damaged, and a second fault here would lose the report).
bool UnwindFrame(CONTEXT& context, bool first) {
    __try {
        DWORD64 image_base{};
        const PRUNTIME_FUNCTION function =
            RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
        if (function == nullptr) {
            if (!first) {
                return false;
            }
            context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
            context.Rsp += 8;
            return true;
        }
        PVOID handler_data{};
        DWORD64 establisher_frame{};
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context,
                         &handler_data, &establisher_frame, nullptr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* exception) {
    static std::atomic<bool> entered{};
    if (g_directory[0] == L'\0' || entered.exchange(true)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t stem[MAX_PATH + 64];
    std::swprintf(stem, std::size(stem), L"%ls\\lichii-crash-%04u%02u%02u-%02u%02u%02u-%lu",
                  g_directory, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                  now.wSecond, GetCurrentProcessId());
    wchar_t path[MAX_PATH + 72];

    DWORD dump_error = 0;
    // The dump first: it does not depend on walking a stack that may be damaged.
    if (g_write_dump != nullptr) {
        std::swprintf(path, std::size(path), L"%ls.dmp", stem);
        const HANDLE dump = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dump != INVALID_HANDLE_VALUE) {
            const DumpExceptionInformation information{GetCurrentThreadId(), exception, FALSE};
            // MiniDumpWithThreadInfo (0x1000): stacks and registers of every thread.
            if (!g_write_dump(GetCurrentProcess(), GetCurrentProcessId(), dump, 0x1000,
                              &information, nullptr, nullptr)) {
                dump_error = GetLastError();
            }
            CloseHandle(dump);
        }
    }

    std::swprintf(path, std::size(path), L"%ls.txt", stem);
    const HANDLE text = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (text == INVALID_HANDLE_VALUE) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const EXCEPTION_RECORD& record = *exception->ExceptionRecord;
    char line[512];
    Write(text, line,
          std::snprintf(line, sizeof(line), "Lichii %s | %s | mejorado %s\r\n",
                        Common::g_build_fullname, Common::g_scm_rev, Common::g_mejorado_version));
    Write(text, line,
          std::snprintf(line, sizeof(line),
                        "Unhandled exception 0x%08lx on thread %lu, parameters 0x%llx 0x%llx\r\n",
                        record.ExceptionCode, GetCurrentThreadId(),
                        static_cast<unsigned long long>(
                            record.NumberParameters > 0 ? record.ExceptionInformation[0] : 0),
                        static_cast<unsigned long long>(
                            record.NumberParameters > 1 ? record.ExceptionInformation[1] : 0)));
    WriteAddress(text, "at", reinterpret_cast<DWORD64>(record.ExceptionAddress));
    if (dump_error != 0) {
        Write(text, line,
              std::snprintf(line, sizeof(line), "Minidump failed: error 0x%08lx\r\n",
                            dump_error));
    }
    FlushFileBuffers(text);

    // The faulting thread's stack from the exception's context (a copy: unwinding changes it).
    CONTEXT context = *exception->ContextRecord;
    for (size_t depth = 0; depth < MaxFrames && context.Rip != 0; ++depth) {
        WriteAddress(text, "frame", context.Rip);
        if (!UnwindFrame(context, depth == 0)) {
            break;
        }
    }
    CloseHandle(text);
    return EXCEPTION_CONTINUE_SEARCH;
}

/// Removes all but the newest reports so crashes cannot fill the disk.
void PruneOldReports(const std::filesystem::path& directory) {
    std::error_code error;
    std::vector<std::filesystem::path> stems;
    for (const auto& entry : std::filesystem::directory_iterator{directory, error}) {
        const auto name = entry.path().filename().wstring();
        if (name.starts_with(L"lichii-crash-") && entry.path().extension() == L".txt") {
            stems.push_back(entry.path().parent_path() / entry.path().stem());
        }
    }
    if (stems.size() <= KeptReports) {
        return;
    }
    // The names start with the date and time, so sorting them is sorting by age.
    std::ranges::sort(stems);
    for (size_t i = 0; i + KeptReports < stems.size(); ++i) {
        for (const wchar_t* extension : {L".txt", L".dmp"}) {
            auto file = stems[i];
            file += extension;
            std::filesystem::remove(file, error);
        }
    }
}

} // Anonymous namespace

void Install() {
    const auto directory = Common::FS::GetEdenPath(Common::FS::EdenPath::CrashDumpsDir);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::wstring wide = directory.wstring();
    if (error || wide.size() >= MAX_PATH - 64) {
        return;
    }
    PruneOldReports(directory);
    std::wmemcpy(g_directory, wide.c_str(), wide.size() + 1);
    // Loaded now: loading a library inside the handler takes the loader lock.
    if (const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll"); dbghelp != nullptr) {
        g_write_dump =
            reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    }
    SetUnhandledExceptionFilter(OnUnhandledException);
}

} // namespace CrashReport

#else

namespace CrashReport {
void Install() {}
} // namespace CrashReport

#endif

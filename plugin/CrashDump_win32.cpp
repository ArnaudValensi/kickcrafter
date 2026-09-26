// Diagnostic build only: see CrashDump.h. Compiled solely when KCF_CRASH_DUMP is ON (Windows).
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "CrashDump.h"

#if defined (_WIN32) && defined (KCF_CRASH_DUMP) && KCF_CRASH_DUMP

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>       // SHGetKnownFolderPath: the Desktop
#include <objbase.h>      // CoTaskMemFree
#include <cstdio>
#include <cwchar>

#ifndef KCF_VERSION_STRING
#define KCF_VERSION_STRING "unknown"
#endif

namespace
{
    LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
    bool installed = false;
    wchar_t dumpPath[MAX_PATH] = {};
    wchar_t notePath[MAX_PATH] = {};

    bool selfTestRequested() noexcept
    {
        return GetEnvironmentVariableW (L"KCF_CRASH_TEST", nullptr, 0) > 0;
    }

    void writeNote (EXCEPTION_POINTERS* info) noexcept
    {
        FILE* note = nullptr;
        if (_wfopen_s (&note, notePath, L"w") != 0 || note == nullptr) return;
        HMODULE module = nullptr;
        GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR> (&writeNote), &module);
        wchar_t moduleFile[MAX_PATH] = {};
        if (module != nullptr) GetModuleFileNameW (module, moduleFile, MAX_PATH);
        std::fwprintf (note, L"KickCrafter diagnostic build %hs\n", KCF_VERSION_STRING);
        std::fwprintf (note, L"module: %ls\nmodule base: %p\n", moduleFile, (void*) module);
        if (info != nullptr && info->ExceptionRecord != nullptr)
            std::fwprintf (note, L"exception code: 0x%08lX at %p\n", (unsigned long) info->ExceptionRecord->ExceptionCode,
                           info->ExceptionRecord->ExceptionAddress);
        std::fwprintf (note, L"dump: %ls\n", dumpPath);
        std::fclose (note);
    }

    LONG WINAPI writeCrashDump (EXCEPTION_POINTERS* info) noexcept
    {
        // The folder: KCF_CRASH_DUMP_DIR when set (an escape hatch), else the user's Desktop
        // (where a tester finds the file without being told a path), else %TEMP%.
        wchar_t temp[MAX_PATH] = {};
        DWORD length = GetEnvironmentVariableW (L"KCF_CRASH_DUMP_DIR", temp, MAX_PATH);
        if (length == 0 || length >= MAX_PATH - 1)
        {
            length = 0;
            PWSTR desktop = nullptr;
            if (SUCCEEDED (SHGetKnownFolderPath (FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &desktop)) && desktop != nullptr)
            {
                if (wcslen (desktop) < MAX_PATH - 2) { wcscpy_s (temp, MAX_PATH, desktop); length = (DWORD) wcslen (temp); }
                CoTaskMemFree (desktop);
            }
            if (length == 0) length = GetTempPathW (MAX_PATH, temp);
        }
        if (length > 0 && length < MAX_PATH - 1 && temp[length - 1] != L'\\' && temp[length - 1] != L'/')
        {
            temp[length] = L'\\'; temp[length + 1] = 0;
        }
        if (length > 0)
        {
            SYSTEMTIME t; GetLocalTime (&t);
            _snwprintf_s (dumpPath, MAX_PATH, _TRUNCATE, L"%lsKickCrafter-crash-%04u%02u%02u-%02u%02u%02u.dmp",
                          temp, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
            _snwprintf_s (notePath, MAX_PATH, _TRUNCATE, L"%lsKickCrafter-crash-%04u%02u%02u-%02u%02u%02u.txt",
                          temp, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
            HANDLE file = CreateFileW (dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                MINIDUMP_EXCEPTION_INFORMATION exception { GetCurrentThreadId(), info, FALSE };
                const auto type = (MINIDUMP_TYPE) (MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo);
                MiniDumpWriteDump (GetCurrentProcess(), GetCurrentProcessId(), file, type, info != nullptr ? &exception : nullptr, nullptr, nullptr);
                CloseHandle (file);
                writeNote (info);
                if (! selfTestRequested())   // the self-test on CI has nobody to click OK
                {
                    wchar_t text[2 * MAX_PATH + 160];
                    _snwprintf_s (text, sizeof (text) / sizeof (text[0]), _TRUNCATE,
                                  L"KickCrafter (diagnostic build) wrote a crash dump on your Desktop:\n\n%ls\n\nPlease send that file and the .txt next to it.", dumpPath);
                    MessageBoxW (nullptr, text, L"KickCrafter crash dump", MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
                }
            }
        }
        return previousFilter != nullptr ? previousFilter (info) : EXCEPTION_CONTINUE_SEARCH;
    }

    struct Restorer
    {
        ~Restorer() { if (installed) SetUnhandledExceptionFilter (previousFilter); }
    } restorer;
}

void kcf::crashdump::install() noexcept
{
    if (installed) return;
    previousFilter = SetUnhandledExceptionFilter (writeCrashDump);
    installed = true;
}

void kcf::crashdump::selfTestIfRequested() noexcept
{
    if (selfTestRequested())
        RaiseException (EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

#endif

// Diagnostic build only (CMake option KCF_CRASH_DUMP, Windows): a crash of the host process
// writes a minidump to %TEMP% so a tester's crash can be read with the .pdb of the same build.
// Off by default and never in a release: a plug-in that installs a process-wide exception
// filter is trespassing on the host. epics/cubase-crash, way 3.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace kcf::crashdump
{
    // Installs the unhandled-exception filter once (idempotent); restored when the module unloads.
    void install() noexcept;
    // With the environment variable KCF_CRASH_TEST set, raises an access violation on purpose:
    // the self-test of the mechanism (CI runs it on the diagnostic build and expects the dump).
    void selfTestIfRequested() noexcept;
}

#if ! defined (KCF_CRASH_DUMP) || ! KCF_CRASH_DUMP
inline void kcf::crashdump::install() noexcept {}
inline void kcf::crashdump::selfTestIfRequested() noexcept {}
#endif

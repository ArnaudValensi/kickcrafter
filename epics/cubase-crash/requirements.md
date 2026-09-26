# The Cubase 11 crash on Windows: reproduce, then read

Status: in progress (2026-09-26). An investigation epic: its requirements are the three ways to
reach the crash, decided with the owner on 2026-09-26, in order; it closes when the cause is
known and fixed, or when every way has been tried and the finding recorded.

The report: a tester on Windows (10, unconfirmed) with Cubase 11 inserts KickCrafter 1.5.3, the
editor opens, a message flashes too briefly to read, and the whole of Cubase dies. The CI checks
added by `epics/archive/windows-validation` (Steinberg validator, pluginval with the editor open,
on a Windows runner) do not reproduce it. Cubase 11 itself cannot be run here: no trial is
distributed any more, its eLicenser does not work under Wine, and there is no licence.

Read `epics/archive/windows-validation/requirements.md` first, then `tests/vst3_host_lifecycle.cpp`,
`tools/fetch-validators.sh`, `tools/editorhost.sh`, `.github/workflows/build.yml`,
`plugin/PluginEditor.cpp` (the resize policy: fixed aspect ratio, 80 to 160 %, the `%` menu).

## The three ways, in order

1. **The Cubase sequence in the Linux VST3 host test** (done first, 2026-09-26). Cubase on a
   Windows screen scaled above 100 % gives the view its content scale factor
   (`IPlugViewContentScaleSupport`) and negotiates every size through `checkSizeConstraint`
   before `onSize`. `kcf_vst3_host` now plays that sequence on the real module through the
   public interfaces: scale factors 1.25, 1.5, 2.0 and back to 1.0, after each the reported
   size applied, then four proposals the editor's constrainer must correct (off aspect, below
   and above the limits), each corrected size re-checked as a fixed point, with a budget of
   sixteen `resizeView` callbacks per factor beyond which the frame refuses and the run fails
   (a recursion would crash the process). JUCE's `setContentScaleFactor` is the same code on
   Linux and Windows except for a Cubase 10 special case, so a loop in the wrapper or in the
   editor's constrainer would show here. It runs inside `./run memory` pass A and pass B.

2. **Steinberg's editorhost on the CI runners** (macOS and Windows). The SDK sample whose window
   code is the closest public relative of Cubase's, built from the pinned SDK with the hosting
   examples on, in its own build tree, cached; `./run editorhost 20 <bundle>` opens the editor
   and passes when the host is alive after 20 s, fails with the host's exit code when it died
   before. Not on Linux (gtkmm). The runners' screens are at 100 %, so this covers the attach
   and the negotiation with Steinberg's frame implementation, not the DPI factor (way 1 does).

3. **A diagnostic build for the tester.** For the next build sent to the tester: a Windows build
   whose module installs an unhandled-exception filter at load and writes a minidump
   (`MiniDumpWriteDump`, `%TEMP%\KickCrafter-crash-<time>.dmp`) plus a text note naming the
   module and the build, then lets the crash continue. Only in that build (a CMake option
   `KCF_CRASH_DUMP`, off by default, never in a release), because a plug-in that installs a
   process-wide exception filter is trespassing on the host. With the `.pdb` of the same build,
   the dump gives the exact stack. The tester's own path stays the fallback: the Windows
   `LocalDumps` registry key for Cubase's executable, and the Event Viewer's "Application Error"
   entry (faulting module and offset), which the guide describes.

## Also decided

- **The `%` menu goes, and the fixed aspect ratio with it** (the owner, 2026-09-26): the editor
  is vector-drawn and scales continuously, so dynamic resizing alone serves the user; and
  accepting any size the host proposes (scale from the smaller of the two ratios, content
  centred) removes the one place where a host and a constrainer can disagree. Done after ways 1
  and 2, as its own change with `./run validate`, unless way 1 or 2 finds the cause elsewhere
  first. The saved UI size stays in the state (the percent from the smaller ratio).
- The finding of each way is recorded in this epic's journal the day it is made; the epic is
  archived when the crash is explained or when the tester's dump has been read.

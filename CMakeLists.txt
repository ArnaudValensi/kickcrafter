# KickCrafter Fable - reproducible CMake build.
# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.22)

# macOS: one universal binary (Apple Silicon and Intel) that runs from macOS 11, the first release
# with Apple Silicon. Both variables must be set before project() and only when the caller set
# nothing: the cache or -D, or the environment variables CMake itself reads for them
# (CMAKE_OSX_ARCHITECTURES and MACOSX_DEPLOYMENT_TARGET), which project() then honours. APPLE does
# not exist yet at this point; CMAKE_HOST_APPLE does, and the macOS build is native (no
# cross-compilation), so the host is the target.
if(CMAKE_HOST_APPLE)
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES AND NOT DEFINED ENV{CMAKE_OSX_ARCHITECTURES})
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures (universal by default)")
    endif()
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET AND NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    endif()
endif()

project(KickCrafterFable VERSION 1.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(KCF_BUILD_PLUGIN "Build the JUCE VST3/Standalone plugin" ON)
option(KCF_BUILD_TESTS "Build the automated tests" ON)
option(KCF_SANITIZE "Instrument the engine tests with ASan/UBSan" OFF)
option(KCF_ENGINE_ONLY "Only build the engine and engine tests (no JUCE)" OFF)
# Diagnostic memory build (the memory gate, docs/development.md): AddressSanitizer + LeakSanitizer compiled into the
# engine, the plugin shared code, the JUCE modules it contains, the VST3 wrapper and every test
# executable. Frame pointers kept, LTO off (tools/memory-check.sh; never the delivered binary).
option(KCF_SANITIZE_PLUGIN "Instrument engine + plugin + JUCE + tests with ASan/LSan" OFF)

# Both sanitizer builds are the memory gate, which runs on Linux only (LeakSanitizer preload,
# GCC options, the X11 test executables): refuse them anywhere else rather than fail obscurely.
if((KCF_SANITIZE OR KCF_SANITIZE_PLUGIN) AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "KCF_SANITIZE and KCF_SANITIZE_PLUGIN are Linux-only (the memory gate, docs/development.md)")
endif()

# Warning options follow the compiler, not the platform: Apple Clang takes the GCC-style set,
# MSVC has its own. The tests' options live in tests/CMakeLists.txt with the same rule.
if(MSVC)
    set(KCF_WARNING_OPTIONS /W4)
else()
    set(KCF_WARNING_OPTIONS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

# Pinned dependency: JUCE 8.0.9 (tag f72bad64d29715216226685810c5196bd0d79d77).
# The tree is expected at external/JUCE; tools/fetch-juce.sh clones it.
set(KCF_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" CACHE PATH "JUCE source tree")
set(KCF_JUCE_EXPECTED_COMMIT "f72bad64d29715216226685810c5196bd0d79d77")

# ---------------------------------------------------------------- engine ----
add_library(kcf_engine STATIC
    engine/KickEngine.cpp
    engine/KickEngine.h
    engine/KickParams.h
    engine/SoftClip.h)
target_include_directories(kcf_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(kcf_engine PRIVATE ${KCF_WARNING_OPTIONS})

if(KCF_SANITIZE)
    foreach(t kcf_engine)
        target_compile_options(${t} PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${t} PUBLIC -fsanitize=address,undefined)
    endforeach()
endif()
if(KCF_SANITIZE_PLUGIN)
    # Every translation unit of the diagnostic build keeps frame pointers and line tables at -O1 and
    # links the ASan/LSan runtime (leak detection is process-wide). ASan *instrumentation* is applied
    # to this project's own sources and tests only: instrumenting JUCE's unity module TUs needs more
    # than 2 GB per compiler process and was OOM-killed on the 8 GB development machine.
    # This is documented in docs/development.md (memory gate).
    add_library(kcf_memory_instrumentation INTERFACE)
    target_compile_options(kcf_memory_instrumentation INTERFACE -fno-omit-frame-pointer -g1 -O1)
    target_link_options(kcf_memory_instrumentation INTERFACE -fsanitize=address)
    target_link_libraries(kcf_engine PUBLIC kcf_memory_instrumentation)
    target_compile_options(kcf_engine PRIVATE -fsanitize=address)
    set(KCF_INSTRUMENTED_PLUGIN_SOURCES
        plugin/Parameters.cpp plugin/Presets.cpp plugin/PluginProcessor.cpp plugin/PluginEditor.cpp
        plugin/ui/Theme.cpp plugin/ui/Knob.cpp plugin/ui/Graphs.cpp plugin/ui/TopBar.cpp)
    set_source_files_properties(${KCF_INSTRUMENTED_PLUGIN_SOURCES} PROPERTIES COMPILE_OPTIONS "-fsanitize=address")
endif()

# ---------------------------------------------------------------- plugin ----
if(KCF_BUILD_PLUGIN AND NOT KCF_ENGINE_ONLY)
    if(NOT EXISTS "${KCF_JUCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "JUCE not found at ${KCF_JUCE_DIR}. Run tools/fetch-juce.sh first.")
    endif()
    set(KCF_JUCE_COMMIT "")
    if(EXISTS "${KCF_JUCE_DIR}/.kcf-pinned-commit")
        file(READ "${KCF_JUCE_DIR}/.kcf-pinned-commit" KCF_JUCE_COMMIT)
        string(STRIP "${KCF_JUCE_COMMIT}" KCF_JUCE_COMMIT)
    else()
        find_package(Git QUIET)
        if(GIT_EXECUTABLE)
            execute_process(COMMAND ${GIT_EXECUTABLE} -C "${KCF_JUCE_DIR}" rev-parse HEAD
                            OUTPUT_VARIABLE KCF_JUCE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        endif()
    endif()
    if(NOT KCF_JUCE_COMMIT STREQUAL KCF_JUCE_EXPECTED_COMMIT)
        message(WARNING "JUCE commit '${KCF_JUCE_COMMIT}' differs from the pinned ${KCF_JUCE_EXPECTED_COMMIT}")
    endif()

    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${KCF_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)

    juce_add_binary_data(KickCrafterFableAssets
        HEADER_NAME KcfAssets.h
        NAMESPACE KcfAssets
        SOURCES
            resources/fonts/IBMPlexSans-Regular.ttf
            resources/fonts/IBMPlexSans-Medium.ttf
            resources/fonts/IBMPlexSans-SemiBold.ttf
            resources/fonts/IBMPlexMono-Medium.ttf
            resources/presets/01-reference.xml
            resources/presets/02-deep-sub.xml
            resources/presets/03-techno-punch.xml
            resources/presets/04-tight-click.xml
            resources/presets/05-soft-thump.xml
            resources/presets/06-square-growl.xml
            resources/presets/07-long-boom.xml
            resources/presets/08-gabber.xml)

    juce_add_plugin(KickCrafterFable
        PRODUCT_NAME "KickCrafter Fable"
        COMPANY_NAME "Arnaud Valensi"
        COMPANY_COPYRIGHT "Copyright (c) 2026 Arnaud Valensi. AGPL-3.0-or-later."
        BUNDLE_ID "com.arnaudvalensi.kickcrafter-fable"
        PLUGIN_MANUFACTURER_CODE Arnv
        PLUGIN_CODE Kcfb
        FORMATS VST3 AU Standalone   # AU only exists on macOS: JUCE drops it elsewhere
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Synth Drum
        VST3_AUTO_MANIFEST FALSE
        DESCRIPTION "One-kick wavetable synthesiser with frozen per-note snapshots")

    target_sources(KickCrafterFable PRIVATE
        plugin/Parameters.cpp
        plugin/Parameters.h
        plugin/Presets.cpp
        plugin/Presets.h
        plugin/PluginProcessor.cpp
        plugin/PluginProcessor.h
        plugin/PluginEditor.cpp
        plugin/PluginEditor.h
        plugin/ui/Theme.cpp
        plugin/ui/Theme.h
        plugin/ui/Knob.cpp
        plugin/ui/Knob.h
        plugin/ui/PolledBinding.h
        plugin/ui/Graphs.cpp
        plugin/ui/Graphs.h
        plugin/ui/TopBar.cpp
        plugin/ui/TopBar.h)

    target_include_directories(KickCrafterFable PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

    target_compile_definitions(KickCrafterFable PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # v1.1: no synthetic "MIDI CC c|n" parameters (2080 rows in the host's parameter
        # list, deliberately removed). Consequence: VST3 has no raw MIDI CC events, so CC 120/123
        # no longer reach the plug-in from a VST3 host; the processor still handles them (tests,
        # and the REAPER `panic` stage of docs/development.md) and one-shot hits end by
        # themselves anyway.
        JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        KCF_VERSION_STRING="${PROJECT_VERSION}")

    target_link_libraries(KickCrafterFable
        PRIVATE
            kcf_engine
            KickCrafterFableAssets
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_gui_basics
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            $<$<NOT:$<BOOL:${KCF_SANITIZE_PLUGIN}>>:juce::juce_recommended_lto_flags>
            juce::juce_recommended_warning_flags)
    if(KCF_SANITIZE_PLUGIN)
        target_link_libraries(KickCrafterFable PUBLIC kcf_memory_instrumentation)
    endif()

    # Keep the engine visible to the test executables that link the shared code.
    target_link_libraries(KickCrafterFable PUBLIC kcf_engine)
endif()

# ----------------------------------------------------------------- tests ----
if(KCF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

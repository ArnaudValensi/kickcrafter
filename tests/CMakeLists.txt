# SPDX-License-Identifier: AGPL-3.0-or-later
add_executable(kcf_engine_tests engine_tests.cpp)
target_link_libraries(kcf_engine_tests PRIVATE kcf_engine)
target_compile_options(kcf_engine_tests PRIVATE -Wall -Wextra)
add_test(NAME engine_tests COMMAND kcf_engine_tests)

if(KCF_BUILD_PLUGIN AND NOT KCF_ENGINE_ONLY)
    # The plugin tests drive the real KickCrafterProcessor/Editor. They reuse the
    # already compiled shared-code static library of the plugin (same objects the
    # VST3 links) instead of recompiling the JUCE modules a second time, so the
    # include directories and definitions are taken from the plugin target and
    # the Linux system libraries JUCE needs are linked explicitly (JUCE loads
    # X11 through dlopen at runtime).
    add_executable(kcf_plugin_tests plugin_tests.cpp)
    target_include_directories(kcf_plugin_tests PRIVATE
        $<TARGET_PROPERTY:KickCrafterFable,INCLUDE_DIRECTORIES>)
    target_compile_definitions(kcf_plugin_tests PRIVATE
        $<TARGET_PROPERTY:KickCrafterFable,COMPILE_DEFINITIONS>
        JUCE_STANDALONE_APPLICATION=1)
    target_compile_options(kcf_plugin_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(KCF_TEST_DEPS REQUIRED IMPORTED_TARGET freetype2 fontconfig alsa)
    target_link_libraries(kcf_plugin_tests PRIVATE
        $<TARGET_FILE:KickCrafterFable>
        KickCrafterFableAssets
        kcf_engine
        PkgConfig::KCF_TEST_DEPS
        ${CMAKE_DL_LIBS} pthread rt)
    add_dependencies(kcf_plugin_tests KickCrafterFable)
    add_test(NAME plugin_tests COMMAND kcf_plugin_tests)

    # Memory-leak exercise (epic/MEMORY.md): same shared code, many lifecycle cycles, run under
    # LeakSanitizer by tools/memory-check.sh (standalone LSan preload on the ordinary build, and
    # the compiled-in ASan/LSan diagnostic build when KCF_SANITIZE_PLUGIN=ON).
    add_executable(kcf_leak_tests leak_tests.cpp)
    target_include_directories(kcf_leak_tests PRIVATE $<TARGET_PROPERTY:KickCrafterFable,INCLUDE_DIRECTORIES>)
    target_compile_definitions(kcf_leak_tests PRIVATE
        $<TARGET_PROPERTY:KickCrafterFable,COMPILE_DEFINITIONS>
        JUCE_STANDALONE_APPLICATION=1)
    target_compile_options(kcf_leak_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(kcf_leak_tests PRIVATE
        $<TARGET_FILE:KickCrafterFable>
        KickCrafterFableAssets
        kcf_engine
        PkgConfig::KCF_TEST_DEPS
        ${CMAKE_DL_LIBS} pthread rt)
    add_dependencies(kcf_leak_tests KickCrafterFable)

    # Minimal real VST3 host (no JUCE): dlopen the built module, run its factory/component/
    # controller/processor/view lifecycle with a Linux IRunLoop and an X11 parent window.
    add_executable(kcf_vst3_host vst3_host_lifecycle.cpp)
    target_include_directories(kcf_vst3_host PRIVATE
        ${KCF_JUCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK)
    target_compile_options(kcf_vst3_host PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-multichar)
    target_link_libraries(kcf_vst3_host PRIVATE X11 ${CMAKE_DL_LIBS} pthread)
endif()

if(KCF_SANITIZE_PLUGIN)
    # Diagnostic memory build: the test sources themselves are ASan-instrumented (see the top-level
    # CMakeLists.txt for why JUCE's module TUs are not).
    foreach(t kcf_engine_tests kcf_plugin_tests kcf_leak_tests kcf_vst3_host)
        if(TARGET ${t})
            target_compile_options(${t} PRIVATE -fsanitize=address)
            target_link_libraries(${t} PRIVATE kcf_memory_instrumentation)
        endif()
    endforeach()
endif()

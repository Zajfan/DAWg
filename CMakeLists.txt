cmake_minimum_required(VERSION 3.25)

project(DAWg
    VERSION 0.1.0
    DESCRIPTION "Audio-first digital audio workstation"
    LANGUAGES C CXX
)

option(DAWG_BUILD_APP "Build the JUCE application and platform tests" ON)
option(DAWG_BUILD_TESTS "Build DAWg tests" OFF)

# Sanitizers for debug builds (Zajfan Standard §6.3, §10.2)
# ASan+UBSan by default; TSan is incompatible with ASan — enable separately.
option(DAWG_ENABLE_TSAN "Use ThreadSanitizer instead of AddressSanitizer (mutually exclusive)" OFF)
if(DAWG_ENABLE_TSAN)
    set(DAWG_SANITIZER_FLAGS "-fsanitize=thread,undefined")
else()
    set(DAWG_SANITIZER_FLAGS "-fsanitize=address,undefined")
endif()

add_subdirectory(engine)
add_subdirectory(project)
add_subdirectory(services)

if(DAWG_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(dawg_engine_core_tests tests/engine/AudioEngineTests.cpp)
    target_link_libraries(dawg_engine_core_tests PRIVATE dawg_engine_core)
    target_compile_features(dawg_engine_core_tests PRIVATE cxx_std_20)
    target_compile_options(dawg_engine_core_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
        $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
    )
    add_test(NAME dawg.engine_core COMMAND dawg_engine_core_tests)

    add_executable(dawg_project_tests tests/project/ProjectTests.cpp)
    target_link_libraries(dawg_project_tests PRIVATE dawg_project_model)
    target_compile_features(dawg_project_tests PRIVATE cxx_std_20)
    target_compile_options(dawg_project_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
        $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
    )
    add_test(NAME dawg.project COMMAND dawg_project_tests)

    add_executable(dawg_project_file_tests tests/services/ProjectFileTests.cpp)
    target_link_libraries(dawg_project_file_tests PRIVATE dawg_services_persistence)
    target_compile_options(dawg_project_file_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
        $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
    )
    add_test(NAME dawg.project_file COMMAND dawg_project_file_tests)
endif()

if(NOT DAWG_BUILD_APP)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(DAWg
    PRODUCT_NAME "DAWg"
    COMPANY_NAME "DAWg Project"
)

target_sources(DAWg
    PRIVATE
        Source/App/Main.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponent.h
        Source/Audio/AudioAdapter.cpp
        Source/Audio/AudioAdapter.h
        Source/Window/WindowRegistry.cpp
        Source/Window/WindowRegistry.h
        Source/Window/WindowTypes.h
        Source/Window/ManagedWindow.cpp
        Source/Window/ManagedWindow.h
        Source/Window/WindowManager.cpp
        Source/Window/WindowManager.h
        Source/Window/NativeWindowState.h
)

if(WIN32)
    set(DAWG_NATIVE_WINDOW_SOURCE Source/Window/NativeWindowState_windows.cpp)
elseif(APPLE)
    set(DAWG_NATIVE_WINDOW_SOURCE Source/Window/NativeWindowState_mac.mm)
elseif(UNIX)
    set(DAWG_NATIVE_WINDOW_SOURCE Source/Window/NativeWindowState_linux.cpp)
else()
    set(DAWG_NATIVE_WINDOW_SOURCE Source/Window/NativeWindowState_fallback.cpp)
endif()
target_sources(DAWg PRIVATE ${DAWG_NATIVE_WINDOW_SOURCE})

target_compile_features(DAWg PRIVATE cxx_std_26)

target_link_libraries(DAWg
    PRIVATE
        dawg_engine_core
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(DAWg PRIVATE X11::X11)
endif()

# Zero-warning policy (Zajfan Standard §8) — applied per-source to avoid
# hitting JUCE's own module source files, which JUCE 9 compiles inline.
set(DAWG_OWN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/MainComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/AudioAdapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Window/WindowRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Window/ManagedWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Window/WindowManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/${DAWG_NATIVE_WINDOW_SOURCE}
)
set_source_files_properties(${DAWG_OWN_SOURCES}
    PROPERTIES COMPILE_OPTIONS
        "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>"
)
# MSVC: whole-target is safe because JUCE uses a separate compilation model there
target_compile_options(DAWg PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

target_compile_options(DAWg PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)
target_link_options(DAWg PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)

target_compile_definitions(DAWg
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

if(DAWG_BUILD_TESTS)
    add_executable(dawg_window_registry_tests
        Tests/WindowRegistryTests.cpp
        Source/Window/WindowRegistry.cpp
    )
    target_include_directories(dawg_window_registry_tests PRIVATE Source/Window)
    target_compile_features(dawg_window_registry_tests PRIVATE cxx_std_26)
    target_compile_options(dawg_window_registry_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
    )
    add_test(NAME dawg.window_registry COMMAND dawg_window_registry_tests)
    set_tests_properties(dawg.window_registry PROPERTIES LABELS "window-manager")

    add_executable(dawg_window_manager_integration_tests
        Tests/WindowManagerIntegrationTests.cpp
        Source/Window/WindowRegistry.cpp
        Source/Window/ManagedWindow.cpp
        Source/Window/WindowManager.cpp
    )
    target_include_directories(dawg_window_manager_integration_tests PRIVATE Source Source/Window)
    target_compile_features(dawg_window_manager_integration_tests PRIVATE cxx_std_26)
    target_compile_definitions(dawg_window_manager_integration_tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1
    )
    target_compile_options(dawg_window_manager_integration_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
    )
    target_link_libraries(dawg_window_manager_integration_tests PRIVATE
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    if(UNIX AND NOT APPLE)
        target_link_libraries(dawg_window_manager_integration_tests PRIVATE X11::X11)
    endif()
    target_sources(dawg_window_manager_integration_tests PRIVATE ${DAWG_NATIVE_WINDOW_SOURCE})
    add_test(NAME dawg.window_manager_integration
        COMMAND dawg_window_manager_integration_tests)
    set_tests_properties(dawg.window_manager_integration PROPERTIES
        LABELS "window-manager"
        SKIP_RETURN_CODE 77
    )
endif()

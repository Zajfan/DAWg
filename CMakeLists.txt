cmake_minimum_required(VERSION 3.25)

project(DAWg
    VERSION 0.1.0
    DESCRIPTION "Audio-first digital audio workstation"
    LANGUAGES C CXX
)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

# VersaTone engine — monorepo sibling, GUI-independent audio core
set(DAWG_ENGINE_ENABLE_SIMD ON  CACHE BOOL "" FORCE)
set(DAWG_ENGINE_BUILD_SHARED OFF CACHE BOOL "" FORCE)
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(DAWG_ENGINE_ENABLE_LTO ON  CACHE BOOL "" FORCE)
else()
    set(DAWG_ENGINE_ENABLE_LTO OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(../VersaTone/engine dawg_engine_build)

juce_add_gui_app(DAWg
    PRODUCT_NAME "DAWg"
    COMPANY_NAME "DAWg Project"
)

target_sources(DAWg
    PRIVATE
        Source/App/Main.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponent.h
        Source/Audio/AudioEngine.cpp
        Source/Audio/AudioEngine.h
)

target_compile_features(DAWg PRIVATE cxx_std_26)

target_link_libraries(DAWg
    PRIVATE
        dawg_engine
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Zero-warning policy (Zajfan Standard §8)
target_compile_options(DAWg PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

# Sanitizers for debug builds (Zajfan Standard §6.3, §10.2)
# ASan+UBSan by default; TSan is incompatible with ASan — enable separately.
option(DAWG_ENABLE_TSAN "Use ThreadSanitizer instead of AddressSanitizer (mutually exclusive)" OFF)
if(DAWG_ENABLE_TSAN)
    set(DAWG_SANITIZER_FLAGS "-fsanitize=thread,undefined")
else()
    set(DAWG_SANITIZER_FLAGS "-fsanitize=address,undefined")
endif()
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

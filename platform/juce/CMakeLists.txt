add_library(dawg_platform_juce STATIC
    audio/JuceAudioDeviceBridge.cpp
    audio/JuceAudioCallbackAdapter.cpp
)
target_include_directories(dawg_platform_juce PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(dawg_platform_juce PUBLIC cxx_std_20)
target_compile_definitions(dawg_platform_juce PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(dawg_platform_juce PUBLIC dawg_engine_core juce::juce_audio_devices)

# JUCE module sources compile in this target too; strict warnings apply only
# to DAWg sources, following the application's existing JUCE 9 convention.
set_source_files_properties(
    audio/JuceAudioDeviceBridge.cpp
    audio/JuceAudioCallbackAdapter.cpp
    PROPERTIES COMPILE_OPTIONS
        "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>"
)
target_compile_options(dawg_platform_juce PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)
target_link_options(dawg_platform_juce PUBLIC
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)

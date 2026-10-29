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
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(DAWg
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

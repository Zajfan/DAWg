target_sources(DAWg PRIVATE
    Application.cpp
    Application.h
    ApplicationState.h
    ../../ui/shell/MainEditorView.cpp
    ../../ui/shell/MainEditorView.h
    ../../ui/shell/EditorViewModel.h
    ../../ui/shell/TransportBar.cpp
    ../../ui/shell/TransportBar.h
    ../../ui/shell/MixerPanel.cpp
    ../../ui/shell/MixerPanel.h
    ../../ui/shell/TimelineView.cpp
    ../../ui/shell/TimelineView.h
    ../../ui/shell/PianoRollView.cpp
    ../../ui/shell/PianoRollView.h
    ../../ui/shell/DspEditorDialog.cpp
    ../../ui/shell/DspEditorDialog.h
    ../../ui/shell/AudioSettingsDialog.cpp
    ../../ui/shell/AudioSettingsDialog.h
    ../../ui/shell/BounceDialog.cpp
    ../../ui/shell/BounceDialog.h
    ../../ui/shell/PluginBrowserPanel.cpp
    ../../ui/shell/PluginBrowserPanel.h
)
target_include_directories(DAWg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ../../ui/shell)
target_link_libraries(DAWg PRIVATE dawg_platform_juce dawg_services_commands dawg_services_media dawg_services_persistence dawg_services_plugins)

if(DAWG_BUILD_TESTS)
    target_sources(DAWg PRIVATE ../../tests/app/MainComponentOwnershipTests.cpp)
    set_source_files_properties(../../tests/app/MainComponentOwnershipTests.cpp
        PROPERTIES COMPILE_OPTIONS
            "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>"
    )
endif()

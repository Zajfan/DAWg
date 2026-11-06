target_sources(DAWg PRIVATE
    Application.cpp
    Application.h
    ApplicationState.h
    ../../ui/shell/MainEditorView.cpp
    ../../ui/shell/MainEditorView.h
    ../../ui/shell/EditorViewModel.h
)
target_include_directories(DAWg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ../../ui/shell)
target_link_libraries(DAWg PRIVATE dawg_platform_juce dawg_services_commands)

if(DAWG_BUILD_TESTS)
    target_sources(DAWg PRIVATE ../../tests/app/MainComponentOwnershipTests.cpp)
endif()

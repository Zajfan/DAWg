# The application shell remains in Source/App until the Task 7 relocation.
target_link_libraries(DAWg PRIVATE dawg_platform_juce)

if(DAWG_BUILD_TESTS)
    # Compile the UI's borrowed-audio contract with the real application.
    target_sources(DAWg PRIVATE ../../tests/app/MainComponentOwnershipTests.cpp)
endif()

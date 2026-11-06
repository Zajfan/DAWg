add_subdirectory(persistence)

add_library(dawg_services_commands STATIC commands/src/ProjectController.cpp)
target_include_directories(dawg_services_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/commands/include)
target_link_libraries(dawg_services_commands PUBLIC dawg_project_model dawg_engine_core)
target_compile_features(dawg_services_commands PUBLIC cxx_std_20)
target_compile_options(dawg_services_commands PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)
target_link_options(dawg_services_commands PUBLIC
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)

if(DAWG_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(dawg_project_controller_tests ../tests/services/ProjectControllerTests.cpp)
    target_link_libraries(dawg_project_controller_tests PRIVATE dawg_services_commands Threads::Threads)
    target_compile_features(dawg_project_controller_tests PRIVATE cxx_std_20)
    target_compile_options(dawg_project_controller_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
        $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
    )
    add_test(NAME dawg.project_controller COMMAND dawg_project_controller_tests)
endif()

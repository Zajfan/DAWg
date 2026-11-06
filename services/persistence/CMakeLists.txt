include(FetchContent)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(dawg_services_persistence STATIC src/ProjectFile.cpp)
target_include_directories(dawg_services_persistence PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dawg_services_persistence
    PUBLIC dawg_project_model
    PRIVATE nlohmann_json::nlohmann_json
)
target_compile_features(dawg_services_persistence PUBLIC cxx_std_23)
target_compile_options(dawg_services_persistence PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)
target_link_options(dawg_services_persistence PUBLIC
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)

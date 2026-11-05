add_library(dawg_project_model STATIC
    src/Project.cpp
    src/ProjectCommand.cpp
)
target_include_directories(dawg_project_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(dawg_project_model PUBLIC cxx_std_20)
target_compile_options(dawg_project_model PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)
# Consumers of the instrumented static library need the sanitizer runtime.
target_link_options(dawg_project_model PUBLIC
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)

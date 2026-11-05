add_library(dawg_engine_core STATIC
    src/AudioEngine.cpp
    src/TransportCommandQueue.cpp
)
target_include_directories(dawg_engine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(dawg_engine_core PUBLIC cxx_std_20)
target_compile_options(dawg_engine_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)
# Consumers of the instrumented static library need the sanitizer runtime.
target_link_options(dawg_engine_core PUBLIC
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)

find_package(Threads REQUIRED)

add_library(dawg_services_media STATIC
    src/AudioMediaService.cpp
    src/AudioRecordService.cpp
    src/AudioResampler.cpp
    src/AudioWarpService.cpp
    src/DiskStreamPool.cpp
    src/MultiTrackDiskStreamer.cpp
    src/OfflineRenderService.cpp
    src/StemSeparatorService.cpp
    src/TrackFreezeManager.cpp
    src/TransientDetector.cpp
    src/WaveformCache.cpp
)
target_include_directories(dawg_services_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dawg_services_media
    PUBLIC dawg_engine_core dawg_project_model
    PRIVATE Threads::Threads
)
target_compile_features(dawg_services_media PUBLIC cxx_std_23)
target_compile_options(dawg_services_media PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)
target_link_options(dawg_services_media PUBLIC
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:${DAWG_SANITIZER_FLAGS}>
)

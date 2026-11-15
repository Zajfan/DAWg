find_package(Threads REQUIRED)

add_library(dawg_engine_core STATIC
    src/AudioEngine.cpp
    src/AutomationCurveEvaluator.cpp
    src/DspGraph.cpp
    src/GraphHandoff.cpp
    src/Metronome.cpp
    src/MidiClockGenerator.cpp
    src/MidiControllerManager.cpp
    src/PlaybackRingBuffer.cpp
    src/RecordRingBuffer.cpp
    src/RenderSnapshotPublisher.cpp
    src/TempoMap.cpp
    src/TransportCommandQueue.cpp
    src/WorkStealingScheduler.cpp
    src/WsolaTimeStretcher.cpp
    src/clap/ClapPluginNode.cpp
    src/dsp/BrickwallLimiterNode.cpp
    src/dsp/DynamicsCompressorNode.cpp
    src/dsp/DrumPadSamplerNode.cpp
    src/dsp/ModulatorRack.cpp
    src/dsp/ParametricEQNode.cpp
    src/dsp/TnhcDelayNode.cpp
    src/dsp/TnhcReverbNode.cpp
    src/dsp/TnhcSynthNode.cpp
)
target_include_directories(dawg_engine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dawg_engine_core PUBLIC Threads::Threads)
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

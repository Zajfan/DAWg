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
    src/SessionMatrixEngine.cpp
    src/TempoMap.cpp
    src/TransportCommandQueue.cpp
    src/WorkStealingScheduler.cpp
    src/WsolaTimeStretcher.cpp
    src/clap/ClapPluginNode.cpp
    src/dsp/AudioTranscriber.cpp
    src/dsp/AudioWarpEngine.cpp
    src/dsp/BinauralSpatialPannerNode.cpp
    src/dsp/BrickwallLimiterNode.cpp
    src/dsp/ConvolutionReverbNode.cpp
    src/dsp/DynamicsCompressorNode.cpp
    src/dsp/DrumPadSamplerNode.cpp
    src/dsp/DspWireGrid.cpp
    src/dsp/DynamicLinearPhaseEQNode.cpp
    src/dsp/GranularCloudNode.cpp
    src/dsp/HarmonicAssistant.cpp
    src/dsp/MasteringMeterNode.cpp
    src/dsp/ModulatorRack.cpp
    src/dsp/MultibandCompressorNode.cpp
    src/dsp/NeuralAmpModelerNode.cpp
    src/dsp/ParametricEQNode.cpp
    src/dsp/PolyphaseOversampler.cpp
    src/dsp/SpatialSurroundPannerNode.cpp
    src/dsp/SpectralSidechainNode.cpp
    src/dsp/TnhcDelayNode.cpp
    src/dsp/TnhcReverbNode.cpp
    src/dsp/TnhcSynthNode.cpp
    src/dsp/TransientExciterNode.cpp
    src/dsp/VocalPitchCorrectorNode.cpp
    src/plugins/AraPluginBridge.cpp
    src/plugins/PluginCrashSupervisor.cpp
)
target_include_directories(dawg_engine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dawg_engine_core PUBLIC Threads::Threads $<$<PLATFORM_ID:Linux>:rt>)
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

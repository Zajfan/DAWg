#include "dawg/plugins/NativePluginCatalog.h"

#include <dawg/engine/dsp/AdaptiveResonanceSuppressorNode.h>
#include <dawg/engine/dsp/AnalogDegraderSuiteNode.h>
#include <dawg/engine/dsp/AnalogLadderRingModSynthNode.h>
#include <dawg/engine/dsp/BrickwallLimiterNode.h>
#include <dawg/engine/dsp/ComplexOscillatorWavefolderNode.h>
#include <dawg/engine/dsp/DynamicLinearPhaseMasteringEQNode.h>
#include <dawg/engine/dsp/DynamicsCompressorNode.h>
#include <dawg/engine/dsp/GranularTextureMorphNode.h>
#include <dawg/engine/dsp/MultibandStereoImagerNode.h>
#include <dawg/engine/dsp/ParametricEQNode.h>
#include <dawg/engine/dsp/PhysicalModelingSynthNode.h>
#include <dawg/engine/dsp/RayTracingAcousticRoomNode.h>
#include <dawg/engine/dsp/TnhcDelayNode.h>
#include <dawg/engine/dsp/TnhcReverbNode.h>
#include <dawg/engine/dsp/TnhcSynthNode.h>
#include <dawg/engine/dsp/TubeTransformerSaturationNode.h>
#include <dawg/engine/dsp/VectorWaveSequencerNode.h>
#include <dawg/engine/dsp/VirusTiPolarSynthNode.h>
#include <dawg/engine/dsp/Juno106SynthNode.h>
#include <dawg/engine/dsp/OmniFmSynthNode.h>
#include <dawg/engine/dsp/SoundFontPlayerNode.h>

#include <algorithm>

namespace dawg::plugins {

const std::vector<NativePluginDef>& NativePluginCatalog::allPlugins()
{
    static const std::vector<NativePluginDef> catalog = {
        // =====================================================================
        // Native Instruments (VSTi)
        // =====================================================================
        {
            .descriptor = {
                .id = "dawg.synth.virus_ti_polar",
                .name = "Access Virus TI Polar Native Synth",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::instrument,
                .isInstrument = true,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Hypersaw Density", 1.0f, 9.0f, 9.0f, "saws"},
                {1, "Hypersaw Detune", 0.0f, 1.0f, 0.42f, "%"},
                {2, "Stereo Spread", 0.0f, 1.0f, 0.90f, "%"},
                {3, "Filter 1 Cutoff", 20.0f, 20000.0f, 4800.0f, "Hz"},
                {4, "Filter 1 Reso", 0.5f, 15.0f, 2.4f, "Q"},
                {5, "Filter 2 Cutoff", 20.0f, 20000.0f, 2200.0f, "Hz"},
                {6, "Drive", 0.0f, 1.0f, 0.35f, "%"},
                {7, "Delay Mix", 0.0f, 1.0f, 0.35f, "%"},
                {8, "Reverb Mix", 0.0f, 1.0f, 0.28f, "%"}
            },
            .presets = {
                {"Polar Hypersaw Lead", {9.0f, 0.42f, 0.90f, 4800.0f, 2.4f, 2200.0f, 0.35f, 0.35f, 0.28f}},
                {"Cyberpunk Reese Bass", {7.0f, 0.55f, 0.60f, 850.0f, 1.8f, 450.0f, 0.65f, 0.15f, 0.10f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.synth.juno_106",
                .name = "Roland Juno-106 Analog Poly Synth",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::instrument,
                .isInstrument = true,
                .isSandboxed = false
            },
            .parameters = {
                {0, "VCF Cutoff", 10.0f, 20000.0f, 3200.0f, "Hz"},
                {1, "VCF Reso", 0.0f, 1.0f, 0.40f, "%"},
                {2, "Sub Level", 0.0f, 1.0f, 0.35f, "%"},
                {3, "PWM Amount", 0.0f, 1.0f, 0.60f, "%"},
                {4, "Chorus Mode", 0.0f, 3.0f, 1.0f, "idx"}
            },
            .presets = {
                {"Juno Lush Strings", {2800.0f, 0.30f, 0.35f, 0.60f, 1.0f}},
                {"80s Poly Brass", {1600.0f, 0.45f, 0.20f, 0.40f, 2.0f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.synth.omni_fm",
                .name = "Yamaha OmniFM 6-Operator Synth",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::instrument,
                .isInstrument = true,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Algorithm", 1.0f, 32.0f, 16.0f, "idx"},
                {1, "Feedback", 0.0f, 1.0f, 0.45f, "%"},
                {2, "Op 1 Level", 0.0f, 1.0f, 0.90f, "%"},
                {3, "Op 2 Level", 0.0f, 1.0f, 0.65f, "%"},
                {4, "Op 3 Level", 0.0f, 1.0f, 0.75f, "%"}
            },
            .presets = {
                {"Solid DX7 Electric Piano", {16.0f, 0.45f, 0.90f, 0.65f, 0.75f}},
                {"Lately / Solid FM Bass", {1.0f, 0.65f, 0.95f, 0.80f, 0.70f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.synth.soundfont_player",
                .name = "SoundFont / Multi-Sample Player",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::instrument,
                .isInstrument = true,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Attack", 1.0f, 1000.0f, 5.0f, "ms"},
                {1, "Release", 10.0f, 3000.0f, 500.0f, "ms"},
                {2, "Filter Cutoff", 20.0f, 20000.0f, 14000.0f, "Hz"}
            },
            .presets = {
                {"Acoustic Concert Grand", {5.0f, 500.0f, 14000.0f}},
                {"Studio Upright Bass", {2.0f, 250.0f, 4500.0f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.synth.buchla_complex",
                .name = "Buchla West-Coast Complex Synth",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::instrument,
                .isInstrument = true,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Carrier Freq", 20.0f, 5000.0f, 220.0f, "Hz"},
                {1, "Modulator Freq", 0.5f, 2000.0f, 110.0f, "Hz"},
                {2, "Through-Zero FM", 0.0f, 10.0f, 1.5f, "idx"},
                {3, "Wavefold Amount", 0.0f, 1.0f, 0.4f, "%"},
                {4, "Fold Symmetry", -1.0f, 1.0f, 0.0f, "bias"},
                {5, "LPG Decay", 10.0f, 2000.0f, 180.0f, "ms"},
                {6, "Lpg Open", 0.0f, 1.0f, 1.0f, "%"}
            },
            .presets = {
                {"West Coast Pluck", {220.0f, 110.0f, 1.2f, 0.6f, 0.1f, 120.0f, 0.0f}},
                {"California FM Bells", {880.0f, 314.0f, 3.5f, 0.7f, -0.2f, 450.0f, 0.0f}},
                {"Deep Analog Drone", {55.0f, 27.5f, 0.8f, 0.9f, 0.3f, 1500.0f, 1.0f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.synth.vector_sequencer",
                .name = "Vector Wavetable Sequencer",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::instrument,
                .isInstrument = true,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Vector Joystick X", -1.0f, 1.0f, 0.0f, "X"},
                {1, "Vector Joystick Y", -1.0f, 1.0f, 0.0f, "Y"},
                {2, "Filter Cutoff", 20.0f, 20000.0f, 4000.0f, "Hz"},
                {3, "Filter Resonance", 0.0f, 1.0f, 0.25f, "Q"},
                {4, "Vector Morph Rate", 0.1f, 20.0f, 1.0f, "Hz"}
            },
            .presets = {
                {"Prophet VS Brass", {0.2f, 0.6f, 3500.0f, 0.3f, 0.5f}},
                {"Evolving Ambient Stasis", {-0.5f, -0.5f, 1200.0f, 0.6f, 0.2f}},
                {"Dynamic Orbital Pad", {0.0f, 0.0f, 6000.0f, 0.4f, 2.0f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.synth.physical_model",
                .name = "Physical Modeling Acoustic Resonator",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::instrument,
                .isInstrument = true,
                .isSandboxed = false
            },
            .parameters = {
                {0, "String Damping", 0.0f, 1.0f, 0.3f, "%"},
                {1, "Body Stiffness", 0.0f, 1.0f, 0.4f, "%"},
                {2, "Pickup Position", 0.0f, 1.0f, 0.2f, "%"},
                {3, "Membrane Radius", 0.05f, 1.0f, 0.3f, "m"},
                {4, "Acoustic Tone", 100.0f, 12000.0f, 3000.0f, "Hz"}
            },
            .presets = {
                {"Concert Harp", {0.15f, 0.2f, 0.18f, 0.2f, 5000.0f}},
                {"Orchestral Marimba", {0.6f, 0.7f, 0.5f, 0.4f, 2200.0f}},
                {"Resonant Cello Bow", {0.05f, 0.5f, 0.1f, 0.8f, 4000.0f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.synth.granular_cloud",
                .name = "Granular Cloud Sampler",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::instrument,
                .isInstrument = true,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Grain Size", 5.0f, 500.0f, 80.0f, "ms"},
                {1, "Grain Density", 2.0f, 120.0f, 40.0f, "gr/s"},
                {2, "Spray Jitter", 0.0f, 1.0f, 0.35f, "%"},
                {3, "Pitch Shimmer", -24.0f, 24.0f, 12.0f, "semi"},
                {4, "Reverse Grains", 0.0f, 1.0f, 0.2f, "%"}
            },
            .presets = {
                {"Lush Ambient Shimmer", {120.0f, 60.0f, 0.4f, 12.0f, 0.3f}},
                {"Granular Time Freeze", {250.0f, 80.0f, 0.2f, 0.0f, 0.1f}},
                {"Scattered Micro-Grains", {15.0f, 100.0f, 0.8f, 7.0f, 0.5f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.synth.analog_ladder",
                .name = "Analog Moog Ladder Synth",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::instrument,
                .isInstrument = true,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Cutoff Freq", 20.0f, 20000.0f, 1200.0f, "Hz"},
                {1, "Resonance", 0.0f, 1.0f, 0.65f, "Q"},
                {2, "Drive Saturation", 0.0f, 24.0f, 6.0f, "dB"},
                {3, "Ring Mod Mix", 0.0f, 1.0f, 0.0f, "%"},
                {4, "Osc Feedback", 0.0f, 1.0f, 0.1f, "%"}
            },
            .presets = {
                {"Fat Minimoog Bass", {450.0f, 0.7f, 12.0f, 0.0f, 0.2f}},
                {"Screaming Ring Lead", {2800.0f, 0.85f, 18.0f, 0.65f, 0.4f}},
                {"Warm Analog Brass", {1800.0f, 0.35f, 3.0f, 0.0f, 0.0f}}
            }
        },

        // =====================================================================
        // Native Audio Effects (VST)
        // =====================================================================
        {
            .descriptor = {
                .id = "dawg.fx.resonance_suppressor",
                .name = "Adaptive Dynamic Resonance Suppressor",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::dynamics,
                .isInstrument = false,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Suppression Depth", 0.0f, 1.0f, 0.5f, "%"},
                {1, "Filter Sharpness", 0.1f, 5.0f, 1.0f, "Q"},
                {2, "Tame Harshness", 0.0f, 1.0f, 0.4f, "%"},
                {3, "Recover Detail", 0.0f, 1.0f, 0.2f, "%"},
                {4, "Spectral Tilt", -6.0f, 6.0f, 0.0f, "dB"},
                {5, "Audition Delta", 0.0f, 1.0f, 0.0f, "solo", true}
            },
            .presets = {
                {"Vocal Harshness Tamer", {0.65f, 1.8f, 0.6f, 0.25f, 0.5f, 0.0f}},
                {"Drum Cymbals De-Ring", {0.8f, 2.5f, 0.7f, 0.1f, -1.0f, 0.0f}},
                {"Acoustic Guitar Smooth", {0.45f, 1.2f, 0.3f, 0.4f, 1.0f, 0.0f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.fx.tube_transformer",
                .name = "Vintage Tube & Transformer Rack",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::dynamics,
                .isInstrument = false,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Drive", 0.0f, 30.0f, 8.0f, "dB"},
                {1, "Tube Bias", 0.0f, 1.0f, 0.35f, "bias"},
                {2, "Power Sag", 0.0f, 1.0f, 0.4f, "%"},
                {3, "Transformer Drive", 0.0f, 2.0f, 0.8f, "x"},
                {4, "Low-End Warmth", 0.0f, 1.0f, 0.5f, "%"},
                {5, "Dry / Wet Mix", 0.0f, 1.0f, 1.0f, "%"}
            },
            .presets = {
                {"12AX7 Triode Warmth", {6.0f, 0.3f, 0.2f, 0.5f, 0.6f, 1.0f}},
                {"EL34 Crushed Sag", {20.0f, 0.5f, 0.85f, 1.2f, 0.4f, 0.8f}},
                {"Steel Core Console Sum", {3.0f, 0.1f, 0.1f, 1.5f, 0.8f, 1.0f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.fx.linear_phase_eq",
                .name = "Dynamic Linear-Phase Mastering EQ",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::eq,
                .isInstrument = false,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Phase Mode", 0.0f, 1.0f, 0.0f, "FIR", true},
                {1, "Sub Bass Freq", 20.0f, 120.0f, 50.0f, "Hz"},
                {2, "Low Mid Freq", 100.0f, 600.0f, 250.0f, "Hz"},
                {3, "High Mid Freq", 1000.0f, 6000.0f, 3000.0f, "Hz"},
                {4, "Air Shelf Freq", 8000.0f, 20000.0f, 12000.0f, "Hz"},
                {5, "Dynamic Range", -12.0f, 12.0f, -4.0f, "dB"}
            },
            .presets = {
                {"Mastering Transparent Firm", {0.0f, 45.0f, 280.0f, 3500.0f, 14000.0f, -3.0f}},
                {"Vocal Air & Tight Lows", {0.0f, 60.0f, 350.0f, 4200.0f, 16000.0f, -6.0f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.fx.multiband_imager",
                .name = "Mastering Multiband Stereo Imager",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::spatial,
                .isInstrument = false,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Low Width", 0.0f, 3.0f, 0.0f, "x"},
                {1, "Low-Mid Width", 0.0f, 3.0f, 1.0f, "x"},
                {2, "High-Mid Width", 0.0f, 3.0f, 1.5f, "x"},
                {3, "High Width", 0.0f, 3.0f, 1.8f, "x"},
                {4, "Mono Bass Cutoff", 40.0f, 300.0f, 140.0f, "Hz"},
                {5, "Haas Widener", 0.0f, 1.0f, 0.0f, "on", true}
            },
            .presets = {
                {"Vinyl Cut Mastering", {0.0f, 0.9f, 1.3f, 1.5f, 150.0f, 0.0f}},
                {"Wide Atmospheric Spread", {0.0f, 1.2f, 2.0f, 2.4f, 120.0f, 1.0f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.fx.analog_degrader",
                .name = "Analog Tape & Vinyl Degrader Suite",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::utility,
                .isInstrument = false,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Wow & Flutter", 0.0f, 1.0f, 0.35f, "%"},
                {1, "Tape Saturation", 0.0f, 1.0f, 0.5f, "%"},
                {2, "Azimuth Tilt", -1.0f, 1.0f, 0.0f, "deg"},
                {3, "Vinyl Dust Pops", 0.0f, 1.0f, 0.25f, "%"},
                {4, "Inner Groove Distortion", 0.0f, 1.0f, 0.3f, "%"}
            },
            .presets = {
                {"Chrome Type II Cassette", {0.25f, 0.45f, 0.1f, 0.05f, 0.1f}},
                {"Worn 33 RPM Vinyl Record", {0.45f, 0.3f, 0.0f, 0.6f, 0.5f}},
                {"Microcassette Dictaphone", {0.8f, 0.9f, -0.4f, 0.3f, 0.8f}}
            }
        },
        {
            .descriptor = {
                .id = "dawg.fx.raytracing_reverb",
                .name = "3D Ray-Tracing Acoustic Room Modeler",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::spatial,
                .isInstrument = false,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Room Length", 2.0f, 100.0f, 18.0f, "m"},
                {1, "Room Width", 2.0f, 80.0f, 12.0f, "m"},
                {2, "Room Height", 2.0f, 30.0f, 8.0f, "m"},
                {3, "Surface Absorption", 0.05f, 0.95f, 0.3f, "%"},
                {4, "Reverb Decay", 0.2f, 15.0f, 2.5f, "s"},
                {5, "Dry / Wet Mix", 0.0f, 1.0f, 0.35f, "%"}
            },
            .presets = {
                {"Symphony Concert Hall", {35.0f, 24.0f, 14.0f, 0.25f, 3.2f, 0.4f}},
                {"Intimate Wooden Studio", {8.0f, 6.0f, 3.5f, 0.55f, 0.8f, 0.25f}},
                {"Gothic Stone Cathedral", {65.0f, 35.0f, 22.0f, 0.12f, 7.5f, 0.5f}}
            }
        },

        // =====================================================================
        // Legacy Built-ins
        // =====================================================================
        {
            .descriptor = {
                .id = "tnhc.parametric_eq",
                .name = "Tnhc 4-Band Parametric EQ",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::eq,
                .isInstrument = false,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Low Freq", 20.0f, 500.0f, 100.0f, "Hz"},
                {1, "Low Gain", -18.0f, 18.0f, 0.0f, "dB"},
                {2, "Mid Freq", 200.0f, 5000.0f, 1000.0f, "Hz"},
                {3, "Mid Gain", -18.0f, 18.0f, 0.0f, "dB"},
                {4, "High Freq", 1000.0f, 20000.0f, 8000.0f, "Hz"},
                {5, "High Gain", -18.0f, 18.0f, 0.0f, "dB"}
            },
            .presets = {
                {"Flat Response", {100.0f, 0.0f, 1000.0f, 0.0f, 8000.0f, 0.0f}}
            }
        },
        {
            .descriptor = {
                .id = "tnhc.studio_compressor",
                .name = "Tnhc Studio Dynamics Compressor",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::dynamics,
                .isInstrument = false,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Threshold", -60.0f, 0.0f, -18.0f, "dB"},
                {1, "Ratio", 1.0f, 20.0f, 4.0f, ":1"},
                {2, "Attack", 0.1f, 100.0f, 15.0f, "ms"},
                {3, "Release", 10.0f, 1000.0f, 100.0f, "ms"}
            },
            .presets = {
                {"Gentle Bus Compression", {-14.0f, 2.5f, 30.0f, 150.0f}}
            }
        },
        {
            .descriptor = {
                .id = "tnhc.master_limiter",
                .name = "Tnhc Brickwall Master Limiter",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::dynamics,
                .isInstrument = false,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Ceiling", -6.0f, 0.0f, -0.3f, "dBFS"},
                {1, "Release", 1.0f, 500.0f, 50.0f, "ms"}
            },
            .presets = {
                {"CD Standard Mastering", {-0.2f, 40.0f}}
            }
        },
        {
            .descriptor = {
                .id = "tnhc.synth",
                .name = "Tnhc Polyphonic Subtractive Synth",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::instrument,
                .isInstrument = true,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Cutoff", 20.0f, 20000.0f, 3000.0f, "Hz"},
                {1, "Resonance", 0.0f, 1.0f, 0.3f, "Q"}
            },
            .presets = {
                {"Default Saw Pad", {3000.0f, 0.3f}}
            }
        },
        {
            .descriptor = {
                .id = "tnhc.plate_reverb",
                .name = "Tnhc Algorithmic Plate Reverb",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::spatial,
                .isInstrument = false,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Decay Time", 0.1f, 10.0f, 1.8f, "s"},
                {1, "Damping", 0.0f, 1.0f, 0.4f, "%"},
                {2, "Dry/Wet", 0.0f, 1.0f, 0.3f, "%"}
            },
            .presets = {
                {"Vocal Plate", {2.2f, 0.35f, 0.25f}}
            }
        },
        {
            .descriptor = {
                .id = "tnhc.stereo_delay",
                .name = "Tnhc Stereo Delay",
                .vendor = "The No-hands Company",
                .version = "1.0.0",
                .path = "",
                .format = PluginFormat::native,
                .category = PluginCategory::spatial,
                .isInstrument = false,
                .isSandboxed = false
            },
            .parameters = {
                {0, "Delay Time", 10.0f, 2000.0f, 375.0f, "ms"},
                {1, "Feedback", 0.0f, 0.95f, 0.45f, "%"},
                {2, "Dry/Wet", 0.0f, 1.0f, 0.35f, "%"}
            },
            .presets = {
                {"Dotted 8th Ping Pong", {375.0f, 0.5f, 0.3f}}
            }
        }
    };

    return catalog;
}

const NativePluginDef* NativePluginCatalog::findPlugin(std::string_view id)
{
    const auto& catalog = allPlugins();
    const auto it = std::find_if(catalog.begin(), catalog.end(), [id](const auto& item) {
        return item.descriptor.id == id;
    });
    return it != catalog.end() ? &(*it) : nullptr;
}

std::vector<PluginDescriptor> NativePluginCatalog::getDescriptors()
{
    const auto& catalog = allPlugins();
    std::vector<PluginDescriptor> desc;
    desc.reserve(catalog.size());
    for (const auto& item : catalog) {
        desc.push_back(item.descriptor);
    }
    return desc;
}

std::unique_ptr<engine::ProcessorNode> NativePluginCatalog::createNode(std::string_view id)
{
    // Synths
    if (id == "dawg.synth.virus_ti_polar")
        return std::make_unique<engine::VirusTiPolarSynthNode>();
    if (id == "dawg.synth.juno_106")
        return std::make_unique<engine::Juno106SynthNode>();
    if (id == "dawg.synth.omni_fm")
        return std::make_unique<engine::OmniFmSynthNode>();
    if (id == "dawg.synth.soundfont_player")
        return std::make_unique<engine::SoundFontPlayerNode>();
    if (id == "dawg.synth.buchla_complex")
        return std::make_unique<engine::dsp::ComplexOscillatorWavefolderNode>();
    if (id == "dawg.synth.vector_sequencer")
        return std::make_unique<engine::dsp::VectorWaveSequencerNode>();
    if (id == "dawg.synth.physical_model")
        return std::make_unique<engine::dsp::PhysicalModelingSynthNode>();
    if (id == "dawg.synth.granular_cloud")
        return std::make_unique<engine::dsp::GranularTextureMorphNode>();
    if (id == "dawg.synth.analog_ladder")
        return std::make_unique<engine::dsp::AnalogLadderRingModSynthNode>();

    // Effects
    if (id == "dawg.fx.resonance_suppressor")
        return std::make_unique<engine::dsp::AdaptiveResonanceSuppressorNode>();
    if (id == "dawg.fx.tube_transformer")
        return std::make_unique<engine::dsp::TubeTransformerSaturationNode>();
    if (id == "dawg.fx.linear_phase_eq")
        return std::make_unique<engine::dsp::DynamicLinearPhaseMasteringEQNode>();
    if (id == "dawg.fx.multiband_imager")
        return std::make_unique<engine::dsp::MultibandStereoImagerNode>();
    if (id == "dawg.fx.analog_degrader")
        return std::make_unique<engine::dsp::AnalogDegraderSuiteNode>();
    if (id == "dawg.fx.raytracing_reverb")
        return std::make_unique<engine::dsp::RayTracingAcousticRoomNode>();

    // Legacy built-ins
    if (id == "tnhc.parametric_eq")
        return std::make_unique<engine::dsp::ParametricEQNode>();
    if (id == "tnhc.studio_compressor")
        return std::make_unique<engine::dsp::DynamicsCompressorNode>();
    if (id == "tnhc.master_limiter")
        return std::make_unique<engine::dsp::BrickwallLimiterNode>();
    if (id == "tnhc.synth")
        return std::make_unique<engine::TnhcSynthNode>();
    if (id == "tnhc.plate_reverb")
        return std::make_unique<engine::TnhcReverbNode>();
    if (id == "tnhc.stereo_delay")
        return std::make_unique<engine::TnhcDelayNode>();

    return nullptr;
}

} // namespace dawg::plugins

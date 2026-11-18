#include <dawg/plugins/ClapPluginLoader.h>
#include <dawg/plugins/PluginScanner.h>
#include <dawg/plugins/NativePluginCatalog.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace dawg::plugins;

void testBuiltInPlugins()
{
    const auto builtins = PluginScanner::getBuiltInPlugins();
    assert(builtins.size() >= 6);

    bool foundSynth = false;
    bool foundEq = false;
    bool foundComp = false;
    bool foundReverb = false;

    for (const auto& p : builtins)
    {
        assert(p.format == PluginFormat::native);
        if (p.id == "tnhc.synth")
        {
            foundSynth = true;
            assert(p.isInstrument);
            assert(p.category == PluginCategory::instrument);
        }
        else if (p.id == "tnhc.parametric_eq")
        {
            foundEq = true;
            assert(! p.isInstrument);
            assert(p.category == PluginCategory::eq);
        }
        else if (p.id == "tnhc.studio_compressor")
        {
            foundComp = true;
            assert(! p.isInstrument);
            assert(p.category == PluginCategory::dynamics);
        }
        else if (p.id == "tnhc.plate_reverb")
        {
            foundReverb = true;
            assert(! p.isInstrument);
            assert(p.category == PluginCategory::spatial);
        }
    }

    assert(foundSynth && foundEq && foundComp && foundReverb);
    (void) foundSynth;
    (void) foundEq;
    (void) foundComp;
    (void) foundReverb;

    // Check expanded native suite
    assert(builtins.size() >= 17);
    const auto* buchlaDef = NativePluginCatalog::findPlugin("dawg.synth.buchla_complex");
    assert(buchlaDef != nullptr);
    assert(buchlaDef->descriptor.isInstrument);
    assert(!buchlaDef->parameters.empty());
    assert(!buchlaDef->presets.empty());
    (void) buchlaDef;

    const auto* resoDef = NativePluginCatalog::findPlugin("dawg.fx.resonance_suppressor");
    assert(resoDef != nullptr);
    assert(!resoDef->descriptor.isInstrument);
    assert(!resoDef->parameters.empty());
    assert(!resoDef->presets.empty());
    (void) resoDef;

    // Test node instantiation
    auto buchlaNode = NativePluginCatalog::createNode("dawg.synth.buchla_complex");
    assert(buchlaNode != nullptr);
    assert(buchlaNode->name() == "West-Coast Complex Oscillator & Wavefolder");

    auto resoNode = NativePluginCatalog::createNode("dawg.fx.resonance_suppressor");
    assert(resoNode != nullptr);
    assert(resoNode->name() == "Adaptive Dynamic Resonance Suppressor");

    auto tubeNode = NativePluginCatalog::createNode("dawg.fx.tube_transformer");
    assert(tubeNode != nullptr);

    auto imagerNode = NativePluginCatalog::createNode("dawg.fx.multiband_imager");
    assert(imagerNode != nullptr);

    std::cout << "testBuiltInPlugins: PASSED\n";
}

void testDirectoryScanning()
{
    const auto tempDir = std::filesystem::temp_directory_path() / "dawg_test_plugins";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);

    const auto clapFile = tempDir / "FabSynth.clap";
    {
        std::ofstream ofs(clapFile);
        ofs << "dummy clap binary content";
    }

    const auto vst3Dir = tempDir / "StudioVerb.vst3";
    std::filesystem::create_directories(vst3Dir);

    const auto scannedClap = PluginScanner::scanClapDirectories({tempDir});
    assert(scannedClap.size() == 1);
    assert(scannedClap[0].name == "FabSynth");
    assert(scannedClap[0].format == PluginFormat::clap);
    assert(scannedClap[0].isInstrument == true);
    assert(scannedClap[0].isSandboxed == true);

    const auto scannedVst3 = PluginScanner::scanVst3Directories({tempDir});
    assert(scannedVst3.size() == 1);
    assert(scannedVst3[0].name == "StudioVerb");
    assert(scannedVst3[0].format == PluginFormat::vst3);
    assert(scannedVst3[0].category == PluginCategory::spatial);

    const auto all = PluginScanner::scanAll({tempDir}, {tempDir});
    assert(all.size() >= 8); // 6 builtins + 1 clap + 1 vst3

    std::filesystem::remove_all(tempDir);
    std::cout << "testDirectoryScanning: PASSED\n";
}

void testClapPluginLoader()
{
    // 1. Empty path handling
    const auto emptyRes = ClapPluginLoader::loadPlugin("");
    assert(! emptyRes.ok());
    assert(! emptyRes.errorMessage.empty());

    // 2. Non-existent library path
    const auto missingRes = ClapPluginLoader::loadPlugin("/invalid/path/to/plugin.clap");
    assert(! missingRes.ok());
    assert(! missingRes.errorMessage.empty());

    // 3. Mock instance testing
    static const clap_plugin_descriptor mockDesc {
        .clap_version = CLAP_VERSION_INIT,
        .id = "tnhc.mock_plugin",
        .name = "Mock Clap Effect",
        .vendor = "TNHC",
        .url = "https://tnhc.audio",
        .manual_url = "",
        .support_url = "",
        .version = "1.0.0",
        .description = "A mock CLAP plugin for testing",
        .features = nullptr
    };

    static clap_param_info mockParam {
        .id = 42,
        .flags = 0,
        .cookie = nullptr,
        .name = "Cutoff",
        .module = "Filter",
        .min_value = 20.0,
        .max_value = 20000.0,
        .default_value = 1000.0
    };

    static const clap_plugin_params mockParamsExt {
        .count = [](const clap_plugin*) -> std::uint32_t { return 1; },
        .get_info = [](const clap_plugin*, std::uint32_t, clap_param_info* info) -> bool {
            *info = mockParam;
            return true;
        },
        .get_value = [](const clap_plugin*, std::uint32_t, double* val) -> bool {
            *val = 1500.0;
            return true;
        },
        .value_to_text = nullptr,
        .text_to_value = nullptr,
        .flush = nullptr
    };

    static bool activated = false;
    static bool resetCalled = false;
    static const clap_plugin mockPlugin {
        .desc = &mockDesc,
        .plugin_data = nullptr,
        .init = [](const clap_plugin*) -> bool { return true; },
        .destroy = [](const clap_plugin*) {},
        .activate = [](const clap_plugin*, double, std::uint32_t, std::uint32_t) -> bool {
            activated = true;
            return true;
        },
        .deactivate = [](const clap_plugin*) {},
        .start_processing = [](const clap_plugin*) -> bool { return true; },
        .stop_processing = [](const clap_plugin*) {},
        .reset = [](const clap_plugin*) { resetCalled = true; },
        .process = nullptr,
        .get_extension = [](const clap_plugin*, const char* id) -> const void* {
            if (std::string(id) == CLAP_EXT_PARAMS)
                return &mockParamsExt;
            return nullptr;
        },
        .on_main_thread = nullptr
    };

    ClapInstance instance(nullptr, nullptr, &mockPlugin);
    assert(instance.isValid());
    assert(instance.pluginName() == "Mock Clap Effect");
    assert(instance.pluginVendor() == "TNHC");
    assert(instance.pluginVersion() == "1.0.0");
    assert(instance.pluginId() == "tnhc.mock_plugin");

    const auto params = instance.getParameters();
    assert(params.size() == 1);
    assert(params[0].id == 42);
    assert(params[0].name == "Cutoff");
    assert(params[0].module == "Filter");
    assert(params[0].minValue == 20.0);
    assert(params[0].maxValue == 20000.0);
    assert(params[0].defaultValue == 1000.0);
    assert(params[0].currentValue == 1500.0);

    double val = 0.0;
    assert(instance.getParameterValue(42, val));
    assert(val == 1500.0);
    (void) val;

    // Test crash mark and restart supervisor
    assert(! instance.isCrashed());
    instance.markCrashed();
    assert(instance.isCrashed());

    assert(instance.restart(48000.0, 512));
    assert(! instance.isCrashed());
    assert(activated);
    assert(resetCalled);
    (void) activated;
    (void) resetCalled;

    std::cout << "testClapPluginLoader: PASSED\n";
}

int main()
{
    testBuiltInPlugins();
    testDirectoryScanning();
    testClapPluginLoader();
    std::cout << "All PluginScanner tests PASSED!\n";
    return 0;
}

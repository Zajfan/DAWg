#include <dawg/plugins/ClapPluginLoader.h>
#include <dawg/plugins/PluginScanner.h>
#include <dawg/plugins/NativePluginCatalog.h>
#include <dawg/plugins/PluginPresetManager.h>
#include <dawg/plugins/ClapPluginExporter.h>

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

void testPluginPresetManager()
{
    const auto tempDir = std::filesystem::temp_directory_path() / "dawg_test_presets";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);

    PluginPresetManager mgr(tempDir);

    // Verify factory presets
    const auto buchlaPresets = mgr.getPresetsForPlugin("dawg.synth.buchla_complex");
    assert(buchlaPresets.size() >= 3);
    const auto* p1 = mgr.findPreset("dawg.synth.buchla_complex", "factory_1");
    assert(p1 != nullptr);
    assert(p1->name == "West Coast Pluck");
    assert(p1->isFactory);
    assert(p1->rating == 5);
    (void) p1;

    // Save user preset
    PluginPreset userPreset {
        .presetId = "ambient_cloud",
        .pluginId = "dawg.synth.buchla_complex",
        .name = "My Ambient Cloud",
        .author = "Alex Producer",
        .category = "Atmosphere",
        .tags = {"ambient", "reverb", "lush"},
        .rating = 4,
        .parameters = {220.0f, 110.0f, 0.5f, 0.8f, 0.0f, 350.0f, 1.0f},
        .stateData = "custom_chunk_data",
        .isFactory = false
    };
    assert(mgr.saveUserPreset(userPreset));

    // Reload from disk
    PluginPresetManager mgr2(tempDir);
    const auto* loaded = mgr2.findPreset("dawg.synth.buchla_complex", "ambient_cloud");
    assert(loaded != nullptr);
    assert(loaded->name == "My Ambient Cloud");
    assert(loaded->rating == 4);
    assert(loaded->stateData == "custom_chunk_data");
    assert(! loaded->isFactory);
    (void) loaded;

    // Search query
    const auto searchRes = mgr2.searchPresets("dawg.synth.buchla_complex", "ambient");
    assert(! searchRes.empty());
    assert(searchRes[0].name == "My Ambient Cloud");

    // Search with tag
    const auto tagRes = mgr2.searchPresets("dawg.synth.buchla_complex", "", {"lush"});
    assert(tagRes.size() == 1);

    // Ratings & Tag manipulation
    assert(mgr2.setPresetRating("dawg.synth.buchla_complex", "ambient_cloud", 5));
    assert(mgr2.addPresetTag("dawg.synth.buchla_complex", "ambient_cloud", "favorite"));
    assert(mgr2.removePresetTag("dawg.synth.buchla_complex", "ambient_cloud", "reverb"));

    const auto* updated = mgr2.findPreset("dawg.synth.buchla_complex", "ambient_cloud");
    assert(updated != nullptr);
    assert(updated->rating == 5);
    assert(std::find(updated->tags.begin(), updated->tags.end(), "favorite") != updated->tags.end());
    assert(std::find(updated->tags.begin(), updated->tags.end(), "reverb") == updated->tags.end());
    (void) updated;

    // A/B comparison switching
    auto& ab = mgr2.getOrAllocABState("track1_slot0");
    ab.setSlotA({1.0f, 2.0f}, "stateA");
    ab.setSlotB({3.0f, 4.0f}, "stateB");
    assert(ab.activeSlot == ABStateSlot::ActiveSlot::slotA);
    assert(ab.activeParams()[0] == 1.0f);
    assert(ab.activeStateData() == "stateA");

    ab.toggleSlot();
    assert(ab.activeSlot == ABStateSlot::ActiveSlot::slotB);
    assert(ab.activeParams()[0] == 3.0f);
    assert(ab.activeStateData() == "stateB");

    ab.copyBToA();
    ab.toggleSlot();
    assert(ab.activeSlot == ABStateSlot::ActiveSlot::slotA);
    assert(ab.activeParams()[0] == 3.0f);

    // Dependency packaging & relocation
    const auto sampleFile1 = tempDir / "kick.wav";
    const auto sampleFile2 = tempDir / "snare.wav";
    {
        std::ofstream(sampleFile1) << "dummy wave 1";
        std::ofstream(sampleFile2) << "dummy wave 2";
    }

    std::vector<ProjectDependencyItem> deps = {
        {sampleFile1.string(), "samples/kick.wav", "sample", 12, true},
        {sampleFile2.string(), "samples/snare.wav", "sample", 12, true},
        {"/nonexistent/missing.wav", "samples/missing.wav", "sample", 0, false}
    };

    const auto bundleTarget = tempDir / "ProjectBundle";
    const auto rep = PluginPresetManager::packageProjectDependencies(deps, bundleTarget);
    assert(! rep.success); // Has 1 missing file
    assert(rep.filesCollected == 2);
    assert(rep.missingFiles.size() == 1);
    assert(std::filesystem::exists(bundleTarget / "samples" / "kick.wav"));
    assert(std::filesystem::exists(bundleTarget / "samples" / "snare.wav"));

    std::filesystem::remove_all(tempDir);
    std::cout << "testPluginPresetManager: PASSED\n";
}

void testClapPluginExporter()
{
    const auto* entry = ClapPluginExporter::getClapEntry();
    assert(entry != nullptr);
    assert(entry->clap_version.major == 1 && entry->clap_version.minor == 2);
    assert(entry->init(nullptr));

    const auto* factoryVoid = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    assert(factoryVoid != nullptr);
    const auto* factory = static_cast<const clap_plugin_factory*>(factoryVoid);
    assert(factory->get_plugin_count(factory) == 4);

    const auto& exportedList = ClapPluginExporter::getExportedPlugins();
    assert(exportedList.size() == 4);
    (void) exportedList;

    for (uint32_t i = 0; i < 4; ++i)
    {
        const auto* desc = factory->get_plugin_descriptor(factory, i);
        assert(desc != nullptr);
        assert(desc->id != nullptr);
        assert(desc->name != nullptr);

        const auto* plugin = factory->create_plugin(factory, nullptr, desc->id);
        assert(plugin != nullptr);
        assert(plugin->init(plugin));
        assert(plugin->activate(plugin, 48000.0, 32, 256));
        assert(plugin->start_processing(plugin));

        // Test parameters extension
        const auto* extVoid = plugin->get_extension(plugin, CLAP_EXT_PARAMS);
        assert(extVoid != nullptr);
        const auto* extParams = static_cast<const clap_plugin_params*>(extVoid);
        assert(extParams->count(plugin) > 0);

        clap_param_info info {};
        assert(extParams->get_info(plugin, 0, &info));
        assert(info.id == 0);
        (void) info;
        (void) extParams;

        double val = 0.0;
        assert(extParams->get_value(plugin, 0, &val));
        (void) val;

        // Test process with stereo buffers
        std::array<float, 64> inL {}, inR {}, outL {}, outR {};
        inL.fill(0.1f);
        inR.fill(0.1f);

        float* inChannels[2] = {inL.data(), inR.data()};
        float* outChannels[2] = {outL.data(), outR.data()};

        clap_audio_buffer inBuf {
            .data32 = inChannels,
            .data64 = nullptr,
            .channel_count = 2,
            .latency = 0,
            .constant_mask = 0
        };
        clap_audio_buffer outBuf {
            .data32 = outChannels,
            .data64 = nullptr,
            .channel_count = 2,
            .latency = 0,
            .constant_mask = 0
        };
        clap_process proc {
            .steady_time = 0,
            .frames_count = 64,
            .transport = nullptr,
            .audio_inputs = &inBuf,
            .audio_outputs = &outBuf,
            .audio_inputs_count = 1,
            .audio_outputs_count = 1,
            .in_events = nullptr,
            .out_events = nullptr
        };

        const auto status = plugin->process(plugin, &proc);
        assert(status == CLAP_PROCESS_CONTINUE);
        (void) status;

        plugin->reset(plugin);
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }

    // Test CLAP bundle exporter
    const auto exportDir = std::filesystem::temp_directory_path() / "dawg_test_clap_export";
    std::filesystem::remove_all(exportDir);
    assert(ClapPluginExporter::exportClapBundle("dawg.export.resotamer", exportDir));
    assert(std::filesystem::exists(exportDir / "ResoTamer_Dynamic_Resonance_Suppressor.clap" / "Contents" / "Resources" / "manifest.json"));
    std::filesystem::remove_all(exportDir);

    std::cout << "testClapPluginExporter: PASSED\n";
}

int main()
{
    testBuiltInPlugins();
    testDirectoryScanning();
    testClapPluginLoader();
    testPluginPresetManager();
    testClapPluginExporter();
    std::cout << "All PluginScanner tests PASSED!\n";
    return 0;
}

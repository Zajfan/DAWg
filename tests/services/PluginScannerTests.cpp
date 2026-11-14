#include <dawg/plugins/PluginScanner.h>

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

int main()
{
    testBuiltInPlugins();
    testDirectoryScanning();
    std::cout << "All PluginScanner tests PASSED!\n";
    return 0;
}

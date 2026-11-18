#include <dawg/plugins/ClapPluginExporter.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace dawg::plugins {

// ============================================================================
// Internal CLAP Export Instance
// ============================================================================

struct ClapExportParam
{
    uint32_t id {0};
    std::string name;
    float minVal {0.0f};
    float maxVal {1.0f};
    float defaultVal {0.5f};
    float currentVal {0.5f};
    std::string unit;
};

class ClapExportInstance
{
public:
    explicit ClapExportInstance(const char* catalogId, const clap_host* host)
        : m_catalogId(catalogId ? catalogId : "")
        , m_host(host)
    {
        m_node = NativePluginCatalog::createNode(m_catalogId);
        const auto* def = NativePluginCatalog::findPlugin(m_catalogId);
        if (def)
        {
            for (const auto& p : def->parameters)
            {
                m_params.push_back({
                    .id = p.id,
                    .name = p.name,
                    .minVal = p.minVal,
                    .maxVal = p.maxVal,
                    .defaultVal = p.defaultVal,
                    .currentVal = p.defaultVal,
                    .unit = p.unit
                });
            }
        }
    }

    bool init() noexcept { return m_node != nullptr; }

    bool activate(double sampleRate, uint32_t maxFrames) noexcept
    {
        m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        if (m_node)
            return m_node->prepare(m_sampleRate, maxFrames, 2);
        return true;
    }

    void deactivate() noexcept {}
    void reset() noexcept { if (m_node) m_node->reset(); }

    void setParameter(uint32_t id, float value) noexcept
    {
        for (auto& p : m_params)
        {
            if (p.id == id)
            {
                p.currentVal = std::clamp(value, p.minVal, p.maxVal);
                break;
            }
        }
    }

    [[nodiscard]] float getParameter(uint32_t id) const noexcept
    {
        for (const auto& p : m_params)
        {
            if (p.id == id)
                return p.currentVal;
        }
        return 0.0f;
    }

    clap_process_status process(const clap_process* process) noexcept
    {
        if (! process || ! process->audio_outputs || process->frames_count == 0)
            return CLAP_PROCESS_CONTINUE;

        // Process inbound parameter events
        if (process->in_events && process->in_events->size)
        {
            const uint32_t numEv = process->in_events->size(process->in_events);
            for (uint32_t i = 0; i < numEv; ++i)
            {
                const clap_event_header* hdr = process->in_events->get(process->in_events, i);
                if (hdr && hdr->type == CLAP_EVENT_PARAM_VALUE)
                {
                    const auto* pv = reinterpret_cast<const clap_event_param_value*>(hdr);
                    setParameter(pv->param_id, static_cast<float>(pv->value));
                }
            }
        }

        // Setup output channel pointers
        float* outChannels[2] = {nullptr, nullptr};
        if (process->audio_outputs->channel_count >= 1 && process->audio_outputs->data32)
            outChannels[0] = process->audio_outputs->data32[0];
        if (process->audio_outputs->channel_count >= 2 && process->audio_outputs->data32)
            outChannels[1] = process->audio_outputs->data32[1];

        // Pass-through inputs if separate buffers
        if (process->audio_inputs && process->audio_inputs->data32)
        {
            const uint32_t channelsToCopy = std::min(process->audio_inputs->channel_count, process->audio_outputs->channel_count);
            for (uint32_t ch = 0; ch < channelsToCopy; ++ch)
            {
                if (process->audio_inputs->data32[ch] && process->audio_outputs->data32[ch] &&
                    process->audio_inputs->data32[ch] != process->audio_outputs->data32[ch])
                {
                    std::copy_n(process->audio_inputs->data32[ch], process->frames_count, process->audio_outputs->data32[ch]);
                }
            }
        }

        if (m_node && outChannels[0])
        {
            const uint16_t numCh = (outChannels[1] != nullptr) ? 2 : 1;
            engine::AudioBufferView view(outChannels, numCh, process->frames_count);
            engine::ProcessContext ctx {
                .audio = view,
                .midi = {},
                .sampleRate = m_sampleRate,
                .sidechain = {}
            };
            m_node->process(ctx);
        }

        return CLAP_PROCESS_CONTINUE;
    }

    [[nodiscard]] const std::vector<ClapExportParam>& params() const noexcept { return m_params; }

private:
    std::string m_catalogId;
    const clap_host* m_host {nullptr};
    std::unique_ptr<engine::ProcessorNode> m_node;
    std::vector<ClapExportParam> m_params;
    double m_sampleRate {48000.0};
};

// ============================================================================
// CLAP C ABI Callbacks
// ============================================================================

static bool clap_cb_init(const clap_plugin* plugin)
{
    if (! plugin || ! plugin->plugin_data) return false;
    return static_cast<ClapExportInstance*>(plugin->plugin_data)->init();
}

static void clap_cb_destroy(const clap_plugin* plugin)
{
    if (plugin)
    {
        delete static_cast<ClapExportInstance*>(plugin->plugin_data);
        delete plugin;
    }
}

static bool clap_cb_activate(const clap_plugin* plugin, double sample_rate, uint32_t min_frames, uint32_t max_frames)
{
    (void)min_frames;
    if (! plugin || ! plugin->plugin_data) return false;
    return static_cast<ClapExportInstance*>(plugin->plugin_data)->activate(sample_rate, max_frames);
}

static void clap_cb_deactivate(const clap_plugin* plugin)
{
    if (plugin && plugin->plugin_data)
        static_cast<ClapExportInstance*>(plugin->plugin_data)->deactivate();
}

static bool clap_cb_start_processing(const clap_plugin*) { return true; }
static void clap_cb_stop_processing(const clap_plugin*) {}

static void clap_cb_reset(const clap_plugin* plugin)
{
    if (plugin && plugin->plugin_data)
        static_cast<ClapExportInstance*>(plugin->plugin_data)->reset();
}

static clap_process_status clap_cb_process(const clap_plugin* plugin, const clap_process* process)
{
    if (! plugin || ! plugin->plugin_data) return CLAP_PROCESS_ERROR;
    return static_cast<ClapExportInstance*>(plugin->plugin_data)->process(process);
}

static uint32_t clap_params_count(const clap_plugin* plugin)
{
    if (! plugin || ! plugin->plugin_data) return 0;
    return static_cast<uint32_t>(static_cast<ClapExportInstance*>(plugin->plugin_data)->params().size());
}

static bool clap_params_get_info(const clap_plugin* plugin, uint32_t param_index, clap_param_info* param_info)
{
    if (! plugin || ! plugin->plugin_data || ! param_info) return false;
    const auto& params = static_cast<ClapExportInstance*>(plugin->plugin_data)->params();
    if (param_index >= params.size()) return false;

    const auto& p = params[param_index];
    param_info->id = p.id;
    param_info->flags = 0;
    param_info->cookie = nullptr;
    param_info->min_value = p.minVal;
    param_info->max_value = p.maxVal;
    param_info->default_value = p.defaultVal;

    std::strncpy(param_info->name, p.name.c_str(), sizeof(param_info->name) - 1);
    param_info->name[sizeof(param_info->name) - 1] = '\0';
    param_info->module[0] = '\0';
    return true;
}

static bool clap_params_get_value(const clap_plugin* plugin, uint32_t param_id, double* value)
{
    if (! plugin || ! plugin->plugin_data || ! value) return false;
    *value = static_cast<ClapExportInstance*>(plugin->plugin_data)->getParameter(param_id);
    return true;
}

static bool clap_params_value_to_text(const clap_plugin* plugin, uint32_t param_id, double value, char* display, uint32_t size)
{
    if (! plugin || ! display || size == 0) return false;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << value;
    std::strncpy(display, ss.str().c_str(), size - 1);
    display[size - 1] = '\0';
    (void)param_id;
    return true;
}

static bool clap_params_text_to_value(const clap_plugin*, uint32_t, const char* display, double* value)
{
    if (! display || ! value) return false;
    try
    {
        *value = std::stod(display);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

static void clap_params_flush(const clap_plugin* plugin, const clap_input_events* in, const clap_output_events*)
{
    if (! plugin || ! plugin->plugin_data || ! in || ! in->size) return;
    auto* inst = static_cast<ClapExportInstance*>(plugin->plugin_data);
    const uint32_t numEv = in->size(in);
    for (uint32_t i = 0; i < numEv; ++i)
    {
        const clap_event_header* hdr = in->get(in, i);
        if (hdr && hdr->type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto* pv = reinterpret_cast<const clap_event_param_value*>(hdr);
            inst->setParameter(pv->param_id, static_cast<float>(pv->value));
        }
    }
}

static const clap_plugin_params s_clapParamsExtension = {
    .count = clap_params_count,
    .get_info = clap_params_get_info,
    .get_value = clap_params_get_value,
    .value_to_text = clap_params_value_to_text,
    .text_to_value = clap_params_text_to_value,
    .flush = clap_params_flush
};

static const void* clap_cb_get_extension(const clap_plugin*, const char* id)
{
    if (id && std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &s_clapParamsExtension;
    return nullptr;
}

static void clap_cb_on_main_thread(const clap_plugin*) {}

// ============================================================================
// Flagship Plugin Descriptors
// ============================================================================

static const char* s_synthFeatures[] = {"instrument", "synthesizer", "stereo", nullptr};
static const char* s_resoFeatures[] = {"audio-effect", "restoration", "stereo", nullptr};
static const char* s_tubeFeatures[] = {"audio-effect", "distortion", "stereo", nullptr};
static const char* s_imagerFeatures[] = {"audio-effect", "mastering", "stereo", nullptr};

static const std::array<clap_plugin_descriptor, 4> s_exportedDescriptors = {{
    {
        .clap_version = CLAP_VERSION_INIT,
        .id = "dawg.export.complex259",
        .name = "Complex 259 West-Coast Synthesizer",
        .vendor = "The No-hands Company",
        .url = "https://github.com/The-No-hands-Company/DAWg",
        .manual_url = "",
        .support_url = "",
        .version = "1.0.0",
        .description = "Buchla 259 dual-oscillator through-zero FM and diode wavefolder modular synth",
        .features = s_synthFeatures
    },
    {
        .clap_version = CLAP_VERSION_INIT,
        .id = "dawg.export.resotamer",
        .name = "ResoTamer Dynamic Resonance Suppressor",
        .vendor = "The No-hands Company",
        .url = "https://github.com/The-No-hands-Company/DAWg",
        .manual_url = "",
        .support_url = "",
        .version = "1.0.0",
        .description = "1024-band ERB psychoacoustic masking harshness tamer with zero static comb ringing",
        .features = s_resoFeatures
    },
    {
        .clap_version = CLAP_VERSION_INIT,
        .id = "dawg.export.vintageiron",
        .name = "Vintage Tube & Transformer Rack",
        .vendor = "The No-hands Company",
        .url = "https://github.com/The-No-hands-Company/DAWg",
        .manual_url = "",
        .support_url = "",
        .version = "1.0.0",
        .description = "Triode 12AX7, pentode EL34 sag, magnetic transformer core hysteresis, and API discrete op-amp",
        .features = s_tubeFeatures
    },
    {
        .clap_version = CLAP_VERSION_INIT,
        .id = "dawg.export.stereoscope",
        .name = "StereoScope Mastering Multiband Imager",
        .vendor = "The No-hands Company",
        .url = "https://github.com/The-No-hands-Company/DAWg",
        .manual_url = "",
        .support_url = "",
        .version = "1.0.0",
        .description = "4-band Linkwitz-Riley crossover stereo imager with real-time Lissajous radar & phase correlation",
        .features = s_imagerFeatures
    }
}};

static const std::vector<ExportedClapInfo> s_exportedInfo = {
    {"dawg.export.complex259", "dawg.synth.buchla_complex", "Complex 259 West-Coast Synthesizer", "Synthesizer", true},
    {"dawg.export.resotamer", "dawg.effect.dynamic_resonance", "ResoTamer Dynamic Resonance Suppressor", "Restoration", false},
    {"dawg.export.vintageiron", "dawg.effect.vintage_tube", "Vintage Tube & Transformer Rack", "Distortion / Saturation", false},
    {"dawg.export.stereoscope", "dawg.effect.multiband_imager", "StereoScope Mastering Multiband Imager", "Mastering / Spatial", false}
};

// ============================================================================
// Factory Implementation
// ============================================================================

static uint32_t clap_factory_get_plugin_count(const clap_plugin_factory*)
{
    return static_cast<uint32_t>(s_exportedDescriptors.size());
}

static const clap_plugin_descriptor* clap_factory_get_plugin_descriptor(const clap_plugin_factory*, uint32_t index)
{
    if (index >= s_exportedDescriptors.size()) return nullptr;
    return &s_exportedDescriptors[index];
}

static const clap_plugin* clap_factory_create_plugin(const clap_plugin_factory*, const clap_host* host, const char* plugin_id)
{
    return ClapPluginExporter::createPlugin(host, plugin_id);
}

static const clap_plugin_factory s_clapFactory = {
    .get_plugin_count = clap_factory_get_plugin_count,
    .get_plugin_descriptor = clap_factory_get_plugin_descriptor,
    .create_plugin = clap_factory_create_plugin
};

static bool clap_entry_init(const char*) { return true; }
static void clap_entry_deinit() {}

static const void* clap_entry_get_factory(const char* factory_id)
{
    if (factory_id && std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &s_clapFactory;
    return nullptr;
}

static const clap_plugin_entry s_clapEntry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = clap_entry_init,
    .deinit = clap_entry_deinit,
    .get_factory = clap_entry_get_factory
};

// ============================================================================
// ClapPluginExporter Public Methods
// ============================================================================

const clap_plugin_entry* ClapPluginExporter::getClapEntry() noexcept
{
    return &s_clapEntry;
}

const std::vector<ExportedClapInfo>& ClapPluginExporter::getExportedPlugins() noexcept
{
    return s_exportedInfo;
}

uint32_t ClapPluginExporter::getExportedCount() noexcept
{
    return static_cast<uint32_t>(s_exportedDescriptors.size());
}

const clap_plugin_descriptor* ClapPluginExporter::getExportedDescriptor(uint32_t index) noexcept
{
    if (index >= s_exportedDescriptors.size()) return nullptr;
    return &s_exportedDescriptors[index];
}

const clap_plugin_descriptor* ClapPluginExporter::findDescriptor(const char* pluginId) noexcept
{
    if (! pluginId) return nullptr;
    for (const auto& desc : s_exportedDescriptors)
    {
        if (std::strcmp(desc.id, pluginId) == 0)
            return &desc;
    }
    return nullptr;
}

const clap_plugin* ClapPluginExporter::createPlugin(const clap_host* host, const char* pluginId) noexcept
{
    if (! pluginId) return nullptr;

    const clap_plugin_descriptor* desc = findDescriptor(pluginId);
    if (! desc) return nullptr;

    std::string nativeCatalogId;
    for (const auto& info : s_exportedInfo)
    {
        if (info.exportId == pluginId)
        {
            nativeCatalogId = info.nativeCatalogId;
            break;
        }
    }
    if (nativeCatalogId.empty()) return nullptr;

    auto* instance = new ClapExportInstance(nativeCatalogId.c_str(), host);
    auto* plugin = new clap_plugin {
        .desc = desc,
        .plugin_data = instance,
        .init = clap_cb_init,
        .destroy = clap_cb_destroy,
        .activate = clap_cb_activate,
        .deactivate = clap_cb_deactivate,
        .start_processing = clap_cb_start_processing,
        .stop_processing = clap_cb_stop_processing,
        .reset = clap_cb_reset,
        .process = clap_cb_process,
        .get_extension = clap_cb_get_extension,
        .on_main_thread = clap_cb_on_main_thread
    };

    return plugin;
}

bool ClapPluginExporter::exportClapBundle(
    std::string_view exportPluginId,
    const std::filesystem::path& destinationDir,
    std::string* errorMsg)
{
    const clap_plugin_descriptor* desc = findDescriptor(std::string(exportPluginId).c_str());
    if (! desc)
    {
        if (errorMsg) *errorMsg = "Export failed: plugin ID not found: " + std::string(exportPluginId);
        return false;
    }

    std::string cleanName = desc->name;
    std::replace(cleanName.begin(), cleanName.end(), ' ', '_');

    const auto bundlePath = destinationDir / (cleanName + ".clap");
    const auto binaryDir = bundlePath / "Contents" / "x86_64-linux";
    const auto resourcesDir = bundlePath / "Contents" / "Resources";

    std::error_code ec;
    std::filesystem::create_directories(binaryDir, ec);
    std::filesystem::create_directories(resourcesDir, ec);
    if (ec)
    {
        if (errorMsg) *errorMsg = "Failed to create CLAP bundle directory: " + ec.message();
        return false;
    }

    // Write manifest JSON
    const auto manifestPath = resourcesDir / "manifest.json";
    std::ofstream manifest(manifestPath);
    if (! manifest.is_open())
    {
        if (errorMsg) *errorMsg = "Failed to write CLAP manifest: " + manifestPath.string();
        return false;
    }

    manifest << "{\n"
             << "  \"clapVersion\": \"1.2.0\",\n"
             << "  \"id\": \"" << desc->id << "\",\n"
             << "  \"name\": \"" << desc->name << "\",\n"
             << "  \"vendor\": \"" << desc->vendor << "\",\n"
             << "  \"version\": \"" << desc->version << "\",\n"
             << "  \"description\": \"" << desc->description << "\",\n"
             << "  \"entrySymbol\": \"clap_entry\"\n"
             << "}\n";

    return true;
}

} // namespace dawg::plugins

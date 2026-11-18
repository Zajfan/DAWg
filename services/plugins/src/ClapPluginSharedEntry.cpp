#include <dawg/plugins/ClapPluginExporter.h>

#if defined(_WIN32)
#define CLAP_EXPORT __declspec(dllexport)
#else
#define CLAP_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

static bool clap_entry_shared_init(const char* path)
{
    const auto* entry = dawg::plugins::ClapPluginExporter::getClapEntry();
    return entry && entry->init ? entry->init(path) : true;
}

static void clap_entry_shared_deinit()
{
    const auto* entry = dawg::plugins::ClapPluginExporter::getClapEntry();
    if (entry && entry->deinit)
        entry->deinit();
}

static const void* clap_entry_shared_get_factory(const char* factory_id)
{
    const auto* entry = dawg::plugins::ClapPluginExporter::getClapEntry();
    return entry && entry->get_factory ? entry->get_factory(factory_id) : nullptr;
}

extern CLAP_EXPORT const clap_plugin_entry clap_entry;

extern CLAP_EXPORT const clap_plugin_entry clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = clap_entry_shared_init,
    .deinit = clap_entry_shared_deinit,
    .get_factory = clap_entry_shared_get_factory
};

}

/**
 * clap-validate: Simple CLAP plugin validation tool
 *
 * Usage: clap-validate <plugin.clap>
 *
 * Tests:
 * - Plugin loads successfully
 * - Factory returns valid descriptors
 * - Plugin can be created, initialized, activated, processed, and destroyed
 */

#include "clap-trap/clap-trap.h"
#include <cstdio>
#include <cstdlib>

using namespace clap_trap;

static constexpr uint32_t SAMPLE_RATE = 48000;
static constexpr uint32_t BLOCK_SIZE = 256;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <plugin.clap>\n", argv[0]);
        return 1;
    }

    const char* pluginPath = argv[1];
    printf("Loading: %s\n", pluginPath);

    // Load plugin
    auto loader = PluginLoader::load(pluginPath);
    if (!loader->entry()) {
        fprintf(stderr, "ERROR: %s\n", loader->getError().c_str());
        return 1;
    }
    printf("  OK: Plugin loaded\n");

    // Get factory
    const auto* factory = loader->factory();
    if (!factory) {
        fprintf(stderr, "ERROR: No plugin factory\n");
        return 1;
    }
    printf("  OK: Got plugin factory\n");

    // Enumerate plugins
    uint32_t count = factory->get_plugin_count(factory);
    printf("  Found %u plugin(s)\n", count);

    if (count == 0) {
        fprintf(stderr, "ERROR: No plugins in factory\n");
        return 1;
    }

    // Test each plugin
    int failures = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto* desc = factory->get_plugin_descriptor(factory, i);
        if (!desc) {
            fprintf(stderr, "  ERROR: Null descriptor for plugin %u\n", i);
            failures++;
            continue;
        }

        printf("\n  Plugin %u: %s\n", i, desc->name);
        printf("    ID: %s\n", desc->id);
        printf("    Vendor: %s\n", desc->vendor);
        printf("    Version: %s\n", desc->version);

        // Create plugin instance
        TestHost host;
        const clap_plugin_t* plugin = factory->create_plugin(factory, host.clapHost(), desc->id);
        if (!plugin) {
            fprintf(stderr, "    ERROR: Failed to create plugin\n");
            failures++;
            continue;
        }
        printf("    OK: Created instance\n");

        // Init
        if (!plugin->init(plugin)) {
            fprintf(stderr, "    ERROR: init() failed\n");
            plugin->destroy(plugin);
            failures++;
            continue;
        }
        printf("    OK: init()\n");

        // Check extensions
        const auto* params = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        if (params) {
            uint32_t paramCount = params->count(plugin);
            printf("    Params: %u\n", paramCount);
        }

        const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
        if (audioPorts) {
            uint32_t inputs = audioPorts->count(plugin, true);
            uint32_t outputs = audioPorts->count(plugin, false);
            printf("    Audio ports: %u in, %u out\n", inputs, outputs);
        }

        // Activate
        if (!plugin->activate(plugin, SAMPLE_RATE, BLOCK_SIZE, BLOCK_SIZE)) {
            fprintf(stderr, "    ERROR: activate() failed\n");
            plugin->destroy(plugin);
            failures++;
            continue;
        }
        printf("    OK: activate(%u Hz, %u frames)\n", SAMPLE_RATE, BLOCK_SIZE);

        // Start processing
        if (!plugin->start_processing(plugin)) {
            fprintf(stderr, "    ERROR: start_processing() failed\n");
            plugin->deactivate(plugin);
            plugin->destroy(plugin);
            failures++;
            continue;
        }
        printf("    OK: start_processing()\n");

        // Process a few blocks
        StereoAudioBuffers buffers(BLOCK_SIZE);
        buffers.fillInputWithSine(440.0f, SAMPLE_RATE);

        EmptyInputEvents inEvents;
        DiscardOutputEvents outEvents;

        clap_process_t process{};
        process.steady_time = 0;
        process.frames_count = BLOCK_SIZE;
        process.transport = nullptr;
        process.audio_inputs = buffers.inputBuffer();
        process.audio_outputs = buffers.outputBuffer();
        process.audio_inputs_count = 1;
        process.audio_outputs_count = 1;
        process.in_events = inEvents.get();
        process.out_events = outEvents.get();

        bool processOk = true;
        for (int block = 0; block < 10; ++block) {
            clap_process_status status = plugin->process(plugin, &process);
            if (status == CLAP_PROCESS_ERROR) {
                fprintf(stderr, "    ERROR: process() returned error at block %d\n", block);
                processOk = false;
                break;
            }
            if (!buffers.outputIsValid()) {
                fprintf(stderr, "    ERROR: Invalid output (NaN/Inf) at block %d\n", block);
                processOk = false;
                break;
            }
            process.steady_time += BLOCK_SIZE;
        }

        if (processOk) {
            printf("    OK: process() x10 blocks\n");
        } else {
            failures++;
        }

        // Stop processing
        plugin->stop_processing(plugin);
        printf("    OK: stop_processing()\n");

        // Deactivate
        plugin->deactivate(plugin);
        printf("    OK: deactivate()\n");

        // Destroy
        plugin->destroy(plugin);
        printf("    OK: destroy()\n");
    }

    printf("\n");
    if (failures == 0) {
        printf("All plugins validated successfully!\n");
        return 0;
    } else {
        printf("FAILED: %d plugin(s) had errors\n", failures);
        return 1;
    }
}

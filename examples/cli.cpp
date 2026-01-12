/**
 * clap-trap CLI
 *
 * A minimal headless CLAP host for automated testing.
 *
 * Commands:
 *   validate <plugin>  - Basic smoke test (load, process, destroy)
 *   info <plugin>      - Dump detailed plugin information
 *   bench <plugin>     - Benchmark processing performance
 */

#include "clap-trap/clap-trap.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace clap_trap;

static constexpr uint32_t DEFAULT_SAMPLE_RATE = 48000;
static constexpr uint32_t DEFAULT_BLOCK_SIZE = 256;

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

static void printUsage(const char* prog) {
    fprintf(stderr, "clap-trap - A minimal headless CLAP host for automated testing\n\n");
    fprintf(stderr, "Usage: %s <command> <plugin.clap> [options]\n\n", prog);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  validate <plugin>   Basic smoke test (load, process, destroy)\n");
    fprintf(stderr, "  info <plugin>       Dump detailed plugin information\n");
    fprintf(stderr, "  bench <plugin>      Benchmark processing performance\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --blocks N          Number of blocks to process (default: 10 for validate, 10000 for bench)\n");
    fprintf(stderr, "  --buffer-size N     Buffer size in samples (default: 256)\n");
    fprintf(stderr, "  --sample-rate N     Sample rate in Hz (default: 48000)\n");
}

struct Options {
    const char* command = nullptr;
    const char* pluginPath = nullptr;
    uint32_t blocks = 0;  // 0 = use default for command
    uint32_t bufferSize = DEFAULT_BLOCK_SIZE;
    uint32_t sampleRate = DEFAULT_SAMPLE_RATE;
};

static bool parseArgs(int argc, char* argv[], Options& opts) {
    if (argc < 3) return false;

    opts.command = argv[1];
    opts.pluginPath = argv[2];

    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--blocks") == 0 && i + 1 < argc) {
            opts.blocks = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--buffer-size") == 0 && i + 1 < argc) {
            opts.bufferSize = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--sample-rate") == 0 && i + 1 < argc) {
            opts.sampleRate = static_cast<uint32_t>(atoi(argv[++i]));
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

//-----------------------------------------------------------------------------
// Commands
//-----------------------------------------------------------------------------

static int cmdInfo(const Options& opts) {
    auto loader = PluginLoader::load(opts.pluginPath);
    if (!loader->entry()) {
        fprintf(stderr, "ERROR: %s\n", loader->getError().c_str());
        return 1;
    }

    const auto* factory = loader->factory();
    if (!factory) {
        fprintf(stderr, "ERROR: No plugin factory\n");
        return 1;
    }

    uint32_t count = factory->get_plugin_count(factory);
    printf("Plugin file: %s\n", opts.pluginPath);
    printf("Plugins: %u\n\n", count);

    TestHost host;

    for (uint32_t i = 0; i < count; ++i) {
        const auto* desc = factory->get_plugin_descriptor(factory, i);
        if (!desc) continue;

        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("Plugin %u: %s\n", i, desc->name);
        printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
        printf("  ID:          %s\n", desc->id);
        printf("  Vendor:      %s\n", desc->vendor);
        printf("  Version:     %s\n", desc->version);
        printf("  URL:         %s\n", desc->url ? desc->url : "(none)");
        printf("  Manual URL:  %s\n", desc->manual_url ? desc->manual_url : "(none)");
        printf("  Support URL: %s\n", desc->support_url ? desc->support_url : "(none)");
        printf("  Description: %s\n", desc->description ? desc->description : "(none)");

        // Features
        if (desc->features && desc->features[0]) {
            printf("  Features:    ");
            for (int f = 0; desc->features[f]; ++f) {
                printf("%s%s", f > 0 ? ", " : "", desc->features[f]);
            }
            printf("\n");
        }

        // Create instance to query extensions
        const clap_plugin_t* plugin = factory->create_plugin(factory, host.clapHost(), desc->id);
        if (!plugin || !plugin->init(plugin)) {
            printf("  (Could not create instance to query extensions)\n\n");
            if (plugin) plugin->destroy(plugin);
            continue;
        }

        // Audio ports
        const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
        if (audioPorts) {
            uint32_t inputCount = audioPorts->count(plugin, true);
            uint32_t outputCount = audioPorts->count(plugin, false);

            printf("\n  Audio Ports:\n");
            for (uint32_t p = 0; p < inputCount; ++p) {
                clap_audio_port_info_t info{};
                if (audioPorts->get(plugin, p, true, &info)) {
                    printf("    [IN %u]  %-20s %u ch\n", p, info.name, info.channel_count);
                }
            }
            for (uint32_t p = 0; p < outputCount; ++p) {
                clap_audio_port_info_t info{};
                if (audioPorts->get(plugin, p, false, &info)) {
                    printf("    [OUT %u] %-20s %u ch\n", p, info.name, info.channel_count);
                }
            }
        }

        // Note ports
        const auto* notePorts = static_cast<const clap_plugin_note_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
        if (notePorts) {
            uint32_t inputCount = notePorts->count(plugin, true);
            uint32_t outputCount = notePorts->count(plugin, false);

            if (inputCount > 0 || outputCount > 0) {
                printf("\n  Note Ports:\n");
                for (uint32_t p = 0; p < inputCount; ++p) {
                    clap_note_port_info_t info{};
                    if (notePorts->get(plugin, p, true, &info)) {
                        printf("    [IN %u]  %-20s\n", p, info.name);
                    }
                }
                for (uint32_t p = 0; p < outputCount; ++p) {
                    clap_note_port_info_t info{};
                    if (notePorts->get(plugin, p, false, &info)) {
                        printf("    [OUT %u] %-20s\n", p, info.name);
                    }
                }
            }
        }

        // Parameters
        const auto* params = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        if (params) {
            uint32_t paramCount = params->count(plugin);
            printf("\n  Parameters: %u\n", paramCount);

            for (uint32_t p = 0; p < paramCount; ++p) {
                clap_param_info_t info{};
                if (params->get_info(plugin, p, &info)) {
                    double value = 0;
                    params->get_value(plugin, info.id, &value);

                    printf("    [%u] %-30s id=%-8u range=[%.2f, %.2f] default=%.2f current=%.2f\n",
                           p, info.name, info.id, info.min_value, info.max_value,
                           info.default_value, value);
                }
            }
        }

        // Extensions supported
        printf("\n  Extensions:\n");
        const char* extensions[] = {
            CLAP_EXT_PARAMS, CLAP_EXT_AUDIO_PORTS, CLAP_EXT_NOTE_PORTS,
            CLAP_EXT_LATENCY, CLAP_EXT_STATE, CLAP_EXT_TAIL,
            CLAP_EXT_RENDER, CLAP_EXT_GUI, nullptr
        };
        for (int e = 0; extensions[e]; ++e) {
            if (plugin->get_extension(plugin, extensions[e])) {
                printf("    ✓ %s\n", extensions[e]);
            }
        }

        plugin->destroy(plugin);
        printf("\n");
    }

    return 0;
}

static int cmdValidate(const Options& opts) {
    uint32_t blocks = opts.blocks > 0 ? opts.blocks : 10;

    auto loader = PluginLoader::load(opts.pluginPath);
    if (!loader->entry()) {
        fprintf(stderr, "ERROR: %s\n", loader->getError().c_str());
        return 1;
    }
    printf("✓ Plugin loaded\n");

    const auto* factory = loader->factory();
    if (!factory) {
        fprintf(stderr, "ERROR: No plugin factory\n");
        return 1;
    }
    printf("✓ Got plugin factory\n");

    uint32_t count = factory->get_plugin_count(factory);
    printf("✓ Found %u plugin(s)\n", count);

    if (count == 0) {
        fprintf(stderr, "ERROR: No plugins in factory\n");
        return 1;
    }

    int failures = 0;
    TestHost host;

    for (uint32_t i = 0; i < count; ++i) {
        const auto* desc = factory->get_plugin_descriptor(factory, i);
        if (!desc) {
            fprintf(stderr, "✗ Null descriptor for plugin %u\n", i);
            failures++;
            continue;
        }

        printf("\n── %s ──\n", desc->name);

        const clap_plugin_t* plugin = factory->create_plugin(factory, host.clapHost(), desc->id);
        if (!plugin) {
            fprintf(stderr, "  ✗ create_plugin() failed\n");
            failures++;
            continue;
        }
        printf("  ✓ create_plugin()\n");

        if (!plugin->init(plugin)) {
            fprintf(stderr, "  ✗ init() failed\n");
            plugin->destroy(plugin);
            failures++;
            continue;
        }
        printf("  ✓ init()\n");

        if (!plugin->activate(plugin, opts.sampleRate, opts.bufferSize, opts.bufferSize)) {
            fprintf(stderr, "  ✗ activate() failed\n");
            plugin->destroy(plugin);
            failures++;
            continue;
        }
        printf("  ✓ activate(%u Hz, %u samples)\n", opts.sampleRate, opts.bufferSize);

        if (!plugin->start_processing(plugin)) {
            fprintf(stderr, "  ✗ start_processing() failed\n");
            plugin->deactivate(plugin);
            plugin->destroy(plugin);
            failures++;
            continue;
        }
        printf("  ✓ start_processing()\n");

        // Process blocks
        StereoAudioBuffers buffers(opts.bufferSize);
        buffers.fillInputWithSine(440.0f, static_cast<float>(opts.sampleRate));

        EmptyInputEvents inEvents;
        DiscardOutputEvents outEvents;

        clap_process_t process{};
        process.steady_time = 0;
        process.frames_count = opts.bufferSize;
        process.transport = nullptr;
        process.audio_inputs = buffers.inputBuffer();
        process.audio_outputs = buffers.outputBuffer();
        process.audio_inputs_count = 1;
        process.audio_outputs_count = 1;
        process.in_events = inEvents.get();
        process.out_events = outEvents.get();

        bool processOk = true;
        for (uint32_t b = 0; b < blocks; ++b) {
            clap_process_status status = plugin->process(plugin, &process);
            if (status == CLAP_PROCESS_ERROR) {
                fprintf(stderr, "  ✗ process() returned error at block %u\n", b);
                processOk = false;
                break;
            }
            if (!buffers.outputIsValid()) {
                fprintf(stderr, "  ✗ Invalid output (NaN/Inf) at block %u\n", b);
                processOk = false;
                break;
            }
            process.steady_time += opts.bufferSize;
        }

        if (processOk) {
            printf("  ✓ process() x%u blocks\n", blocks);
        } else {
            failures++;
        }

        plugin->stop_processing(plugin);
        printf("  ✓ stop_processing()\n");

        plugin->deactivate(plugin);
        printf("  ✓ deactivate()\n");

        plugin->destroy(plugin);
        printf("  ✓ destroy()\n");
    }

    printf("\n");
    if (failures == 0) {
        printf("All %u plugin(s) validated successfully.\n", count);
        return 0;
    } else {
        printf("FAILED: %d plugin(s) had errors.\n", failures);
        return 1;
    }
}

static int cmdBench(const Options& opts) {
    uint32_t blocks = opts.blocks > 0 ? opts.blocks : 10000;

    auto loader = PluginLoader::load(opts.pluginPath);
    if (!loader->entry()) {
        fprintf(stderr, "ERROR: %s\n", loader->getError().c_str());
        return 1;
    }

    const auto* factory = loader->factory();
    if (!factory) {
        fprintf(stderr, "ERROR: No plugin factory\n");
        return 1;
    }

    uint32_t count = factory->get_plugin_count(factory);
    if (count == 0) {
        fprintf(stderr, "ERROR: No plugins in factory\n");
        return 1;
    }

    TestHost host;

    for (uint32_t i = 0; i < count; ++i) {
        const auto* desc = factory->get_plugin_descriptor(factory, i);
        if (!desc) continue;

        const clap_plugin_t* plugin = factory->create_plugin(factory, host.clapHost(), desc->id);
        if (!plugin || !plugin->init(plugin)) {
            if (plugin) plugin->destroy(plugin);
            continue;
        }

        if (!plugin->activate(plugin, opts.sampleRate, opts.bufferSize, opts.bufferSize)) {
            plugin->destroy(plugin);
            continue;
        }

        if (!plugin->start_processing(plugin)) {
            plugin->deactivate(plugin);
            plugin->destroy(plugin);
            continue;
        }

        StereoAudioBuffers buffers(opts.bufferSize);
        buffers.fillInputWithSine(440.0f, static_cast<float>(opts.sampleRate));

        EmptyInputEvents inEvents;
        DiscardOutputEvents outEvents;

        clap_process_t process{};
        process.steady_time = 0;
        process.frames_count = opts.bufferSize;
        process.transport = nullptr;
        process.audio_inputs = buffers.inputBuffer();
        process.audio_outputs = buffers.outputBuffer();
        process.audio_inputs_count = 1;
        process.audio_outputs_count = 1;
        process.in_events = inEvents.get();
        process.out_events = outEvents.get();

        // Warm up
        for (uint32_t b = 0; b < 100; ++b) {
            plugin->process(plugin, &process);
            process.steady_time += opts.bufferSize;
        }

        // Benchmark
        auto start = std::chrono::high_resolution_clock::now();

        for (uint32_t b = 0; b < blocks; ++b) {
            plugin->process(plugin, &process);
            process.steady_time += opts.bufferSize;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        double totalSeconds = duration.count() / 1000000.0;
        double samplesProcessed = static_cast<double>(blocks) * opts.bufferSize;
        double audioSeconds = samplesProcessed / opts.sampleRate;
        double realtime = audioSeconds / totalSeconds;
        double usPerBlock = static_cast<double>(duration.count()) / blocks;

        printf("%-40s %8.1fx realtime  %6.1f µs/block  (%u blocks)\n",
               desc->name, realtime, usPerBlock, blocks);

        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }

    return 0;
}

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    Options opts;

    if (!parseArgs(argc, argv, opts)) {
        printUsage(argv[0]);
        return 1;
    }

    if (strcmp(opts.command, "info") == 0) {
        return cmdInfo(opts);
    } else if (strcmp(opts.command, "validate") == 0) {
        return cmdValidate(opts);
    } else if (strcmp(opts.command, "bench") == 0) {
        return cmdBench(opts);
    } else {
        fprintf(stderr, "Unknown command: %s\n\n", opts.command);
        printUsage(argv[0]);
        return 1;
    }
}

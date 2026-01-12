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
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

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
    fprintf(stderr, "  process <plugin>    Offline audio rendering\n");
    fprintf(stderr, "  state <plugin>      Save/load plugin state\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --blocks N          Number of blocks to process (default: 10 for validate, 10000 for bench)\n");
    fprintf(stderr, "  --buffer-size N     Buffer size in samples (default: 256)\n");
    fprintf(stderr, "  --sample-rate N     Sample rate in Hz (default: 48000)\n");
    fprintf(stderr, "  -i, --input FILE    Input WAV file (process), or state file to load (state)\n");
    fprintf(stderr, "  -o, --output FILE   Output WAV file (process), or state file to save (state)\n");
    fprintf(stderr, "  --float             Output 32-bit float WAV (default: 16-bit PCM)\n");
    fprintf(stderr, "  --roundtrip         Test state save/load round-trip (state command)\n");
}

struct Options {
    const char* command = nullptr;
    const char* pluginPath = nullptr;
    uint32_t blocks = 0;  // 0 = use default for command
    uint32_t bufferSize = DEFAULT_BLOCK_SIZE;
    uint32_t sampleRate = DEFAULT_SAMPLE_RATE;
    const char* inputFile = nullptr;
    const char* outputFile = nullptr;
    bool outputFloat = false;
    bool roundtrip = false;
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
        } else if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) && i + 1 < argc) {
            opts.inputFile = argv[++i];
        } else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            opts.outputFile = argv[++i];
        } else if (strcmp(argv[i], "--float") == 0) {
            opts.outputFloat = true;
        } else if (strcmp(argv[i], "--roundtrip") == 0) {
            opts.roundtrip = true;
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

static int cmdProcess(const Options& opts) {
    if (!opts.outputFile) {
        fprintf(stderr, "ERROR: --output (-o) is required for process command\n");
        return 1;
    }

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

    // Load input audio if provided
    std::unique_ptr<WavFile> inputWav;
    uint32_t inputChannels = 2;
    uint32_t inputFrames = 0;
    uint32_t sampleRate = opts.sampleRate;

    if (opts.inputFile) {
        inputWav = WavFile::load(opts.inputFile);
        if (inputWav->hasError()) {
            fprintf(stderr, "ERROR: %s\n", inputWav->getError().c_str());
            return 1;
        }
        inputChannels = inputWav->channels();
        inputFrames = inputWav->frameCount();
        sampleRate = inputWav->sampleRate();
        printf("Input: %s (%u Hz, %u ch, %u frames)\n",
               opts.inputFile, sampleRate, inputChannels, inputFrames);
    }

    // Determine output length
    uint32_t outputFrames;
    if (inputWav) {
        outputFrames = inputFrames;
    } else {
        // No input: generate specified number of blocks (or default to 1 second)
        uint32_t blocks = opts.blocks > 0 ? opts.blocks : (sampleRate / opts.bufferSize);
        outputFrames = blocks * opts.bufferSize;
    }

    TestHost host;

    // Use first plugin
    const auto* desc = factory->get_plugin_descriptor(factory, 0);
    if (!desc) {
        fprintf(stderr, "ERROR: Null plugin descriptor\n");
        return 1;
    }
    printf("Plugin: %s\n", desc->name);

    const clap_plugin_t* plugin = factory->create_plugin(factory, host.clapHost(), desc->id);
    if (!plugin || !plugin->init(plugin)) {
        fprintf(stderr, "ERROR: Failed to create/init plugin\n");
        if (plugin) plugin->destroy(plugin);
        return 1;
    }

    if (!plugin->activate(plugin, sampleRate, opts.bufferSize, opts.bufferSize)) {
        fprintf(stderr, "ERROR: Failed to activate plugin\n");
        plugin->destroy(plugin);
        return 1;
    }

    if (!plugin->start_processing(plugin)) {
        fprintf(stderr, "ERROR: Failed to start processing\n");
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 1;
    }

    // Query audio ports to get output channel count
    uint32_t outputChannels = 2; // default stereo
    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    if (audioPorts) {
        uint32_t outCount = audioPorts->count(plugin, false);
        if (outCount > 0) {
            clap_audio_port_info_t info{};
            if (audioPorts->get(plugin, 0, false, &info)) {
                outputChannels = info.channel_count;
            }
        }
    }

    // Allocate buffers
    std::vector<float> inputBuffer(opts.bufferSize * inputChannels);
    std::vector<float> outputBuffer(opts.bufferSize * outputChannels);
    std::vector<float> outputSamples;
    outputSamples.reserve(outputFrames * outputChannels);

    // Set up CLAP audio buffers
    std::vector<float*> inChannelPtrs(inputChannels);
    std::vector<float*> outChannelPtrs(outputChannels);
    std::vector<std::vector<float>> inChannels(inputChannels);
    std::vector<std::vector<float>> outChannels(outputChannels);

    for (uint32_t c = 0; c < inputChannels; ++c) {
        inChannels[c].resize(opts.bufferSize);
        inChannelPtrs[c] = inChannels[c].data();
    }
    for (uint32_t c = 0; c < outputChannels; ++c) {
        outChannels[c].resize(opts.bufferSize);
        outChannelPtrs[c] = outChannels[c].data();
    }

    clap_audio_buffer_t inBuf{};
    inBuf.data32 = inChannelPtrs.data();
    inBuf.channel_count = inputChannels;
    inBuf.latency = 0;
    inBuf.constant_mask = 0;

    clap_audio_buffer_t outBuf{};
    outBuf.data32 = outChannelPtrs.data();
    outBuf.channel_count = outputChannels;
    outBuf.latency = 0;
    outBuf.constant_mask = 0;

    EmptyInputEvents inEvents;
    DiscardOutputEvents outEvents;

    clap_process_t process{};
    process.steady_time = 0;
    process.frames_count = opts.bufferSize;
    process.transport = nullptr;
    process.audio_inputs = &inBuf;
    process.audio_outputs = &outBuf;
    process.audio_inputs_count = 1;
    process.audio_outputs_count = 1;
    process.in_events = inEvents.get();
    process.out_events = outEvents.get();

    // Process
    uint32_t framesProcessed = 0;
    uint32_t inputPos = 0;

    while (framesProcessed < outputFrames) {
        uint32_t framesToProcess = std::min(opts.bufferSize, outputFrames - framesProcessed);
        process.frames_count = framesToProcess;

        // Fill input buffers
        if (inputWav) {
            const auto& samples = inputWav->samples();
            for (uint32_t f = 0; f < framesToProcess; ++f) {
                for (uint32_t c = 0; c < inputChannels; ++c) {
                    if (inputPos + f < inputFrames) {
                        inChannels[c][f] = samples[(inputPos + f) * inputChannels + c];
                    } else {
                        inChannels[c][f] = 0.0f;
                    }
                }
            }
        } else {
            // No input: silence
            for (uint32_t c = 0; c < inputChannels; ++c) {
                std::fill(inChannels[c].begin(), inChannels[c].end(), 0.0f);
            }
        }

        // Clear output buffers
        for (uint32_t c = 0; c < outputChannels; ++c) {
            std::fill(outChannels[c].begin(), outChannels[c].end(), 0.0f);
        }

        plugin->process(plugin, &process);

        // Collect output (interleaved)
        for (uint32_t f = 0; f < framesToProcess; ++f) {
            for (uint32_t c = 0; c < outputChannels; ++c) {
                outputSamples.push_back(outChannels[c][f]);
            }
        }

        framesProcessed += framesToProcess;
        inputPos += framesToProcess;
        process.steady_time += framesToProcess;
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);

    // Write output
    WavFormat wavFmt = opts.outputFloat ? WavFormat::Float32 : WavFormat::Int16;
    if (!WavFile::save(opts.outputFile, outputSamples, sampleRate, outputChannels, wavFmt)) {
        fprintf(stderr, "ERROR: Failed to write output file\n");
        return 1;
    }

    printf("Output: %s (%u Hz, %u ch, %u frames, %s)\n",
           opts.outputFile, sampleRate, outputChannels,
           static_cast<uint32_t>(outputSamples.size() / outputChannels),
           opts.outputFloat ? "float32" : "int16");

    return 0;
}

// Stream for state save/load
struct StateStream {
    std::vector<uint8_t> data;
    size_t readPos = 0;
};

static int64_t stateWrite(const clap_ostream_t* stream, const void* buffer, uint64_t size) {
    auto* s = static_cast<StateStream*>(stream->ctx);
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    s->data.insert(s->data.end(), bytes, bytes + size);
    return static_cast<int64_t>(size);
}

static int64_t stateRead(const clap_istream_t* stream, void* buffer, uint64_t size) {
    auto* s = static_cast<StateStream*>(stream->ctx);
    size_t available = s->data.size() - s->readPos;
    size_t toRead = std::min(static_cast<size_t>(size), available);
    if (toRead > 0) {
        std::memcpy(buffer, s->data.data() + s->readPos, toRead);
        s->readPos += toRead;
    }
    return static_cast<int64_t>(toRead);
}

static int cmdState(const Options& opts) {
    if (!opts.outputFile && !opts.inputFile && !opts.roundtrip) {
        fprintf(stderr, "ERROR: state command requires -o (save), -i (load), or --roundtrip\n");
        return 1;
    }

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
    const auto* desc = factory->get_plugin_descriptor(factory, 0);
    if (!desc) {
        fprintf(stderr, "ERROR: Null plugin descriptor\n");
        return 1;
    }
    printf("Plugin: %s\n", desc->name);

    const clap_plugin_t* plugin = factory->create_plugin(factory, host.clapHost(), desc->id);
    if (!plugin || !plugin->init(plugin)) {
        fprintf(stderr, "ERROR: Failed to create/init plugin\n");
        if (plugin) plugin->destroy(plugin);
        return 1;
    }

    // Check for state extension
    const auto* state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    if (!state) {
        fprintf(stderr, "ERROR: Plugin does not support state extension\n");
        plugin->destroy(plugin);
        return 1;
    }

    int result = 0;

    if (opts.roundtrip) {
        // Round-trip test: save state, randomize params, restore, compare
        printf("Testing state round-trip...\n");

        // Save original state
        StateStream saveStream;
        clap_ostream_t ostream{};
        ostream.ctx = &saveStream;
        ostream.write = stateWrite;

        if (!state->save(plugin, &ostream)) {
            fprintf(stderr, "  ERROR: Failed to save state\n");
            plugin->destroy(plugin);
            return 1;
        }
        printf("  Saved state: %zu bytes\n", saveStream.data.size());

        // Get current parameter values
        const auto* params = static_cast<const clap_plugin_params_t*>(
            plugin->get_extension(plugin, CLAP_EXT_PARAMS));

        std::vector<std::pair<clap_id, double>> originalValues;
        if (params) {
            uint32_t paramCount = params->count(plugin);
            for (uint32_t i = 0; i < paramCount; ++i) {
                clap_param_info_t info{};
                if (params->get_info(plugin, i, &info)) {
                    double value = 0;
                    params->get_value(plugin, info.id, &value);
                    originalValues.push_back({info.id, value});
                }
            }
            printf("  Captured %zu parameter values\n", originalValues.size());
        }

        // Restore state
        StateStream loadStream;
        loadStream.data = saveStream.data;
        clap_istream_t istream{};
        istream.ctx = &loadStream;
        istream.read = stateRead;

        if (!state->load(plugin, &istream)) {
            fprintf(stderr, "  ERROR: Failed to load state\n");
            plugin->destroy(plugin);
            return 1;
        }
        printf("  Restored state\n");

        // Compare parameter values
        if (params && !originalValues.empty()) {
            int mismatches = 0;
            for (const auto& [id, expected] : originalValues) {
                double actual = 0;
                params->get_value(plugin, id, &actual);
                if (std::abs(actual - expected) > 1e-6) {
                    fprintf(stderr, "  MISMATCH: param %u: expected %.6f, got %.6f\n",
                            id, expected, actual);
                    mismatches++;
                }
            }
            if (mismatches == 0) {
                printf("  All %zu parameters match after restore\n", originalValues.size());
            } else {
                fprintf(stderr, "  ERROR: %d parameter(s) did not match after restore\n", mismatches);
                result = 1;
            }
        }
    }
    else if (opts.outputFile) {
        // Save state to file
        StateStream stream;
        clap_ostream_t ostream{};
        ostream.ctx = &stream;
        ostream.write = stateWrite;

        if (!state->save(plugin, &ostream)) {
            fprintf(stderr, "ERROR: Failed to save state\n");
            plugin->destroy(plugin);
            return 1;
        }

        std::ofstream file(opts.outputFile, std::ios::binary);
        if (!file) {
            fprintf(stderr, "ERROR: Could not create file: %s\n", opts.outputFile);
            plugin->destroy(plugin);
            return 1;
        }
        file.write(reinterpret_cast<const char*>(stream.data.data()), stream.data.size());
        printf("Saved state: %s (%zu bytes)\n", opts.outputFile, stream.data.size());
    }
    else if (opts.inputFile) {
        // Load state from file
        std::ifstream file(opts.inputFile, std::ios::binary | std::ios::ate);
        if (!file) {
            fprintf(stderr, "ERROR: Could not open file: %s\n", opts.inputFile);
            plugin->destroy(plugin);
            return 1;
        }

        size_t size = file.tellg();
        file.seekg(0);

        StateStream stream;
        stream.data.resize(size);
        file.read(reinterpret_cast<char*>(stream.data.data()), size);

        clap_istream_t istream{};
        istream.ctx = &stream;
        istream.read = stateRead;

        if (!state->load(plugin, &istream)) {
            fprintf(stderr, "ERROR: Failed to load state\n");
            plugin->destroy(plugin);
            return 1;
        }
        printf("Loaded state: %s (%zu bytes)\n", opts.inputFile, size);
    }

    plugin->destroy(plugin);
    return result;
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
    } else if (strcmp(opts.command, "process") == 0) {
        return cmdProcess(opts);
    } else if (strcmp(opts.command, "state") == 0) {
        return cmdState(opts);
    } else {
        fprintf(stderr, "Unknown command: %s\n\n", opts.command);
        printUsage(argv[0]);
        return 1;
    }
}

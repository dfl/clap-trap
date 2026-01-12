# clap-trap

[![CI](https://github.com/dfl/clap-trap/actions/workflows/ci.yml/badge.svg)](https://github.com/dfl/clap-trap/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![GitHub release](https://img.shields.io/github/v/release/dfl/clap-trap)](https://github.com/dfl/clap-trap/releases)

A C++ test harness for CLAP hosts, bridges, and loaders. Also useful if you're the kind of plugin developer who writes integration tests (you should be, but we know you don't).

*It's a trap! ...for catching CLAP plugin bugs.*

## Features

- **Cross-platform plugin loading**: Load `.clap` plugins on macOS, Windows, and Linux
- **Minimal test host**: Implements the required `clap_host_t` interface
- **Audio buffer helpers**: Easy setup for stereo/multi-channel processing tests
- **Event helpers**: Utilities for creating MIDI note and parameter events
- **Validation tool**: Command-line tool to test plugins without a DAW

## Usage

### As a Library

```cpp
#include "clap-trap/clap-trap.h"

using namespace clap_trap;

// Load a plugin
auto loader = PluginLoader::load("/path/to/plugin.clap");
if (!loader->entry()) {
    std::cerr << "Error: " << loader->getError() << std::endl;
    return;
}

// Get the factory and enumerate plugins
const auto* factory = loader->factory();
uint32_t count = factory->get_plugin_count(factory);

// Create a plugin instance
TestHost host;
const auto* desc = factory->get_plugin_descriptor(factory, 0);
const clap_plugin_t* plugin = factory->create_plugin(factory, host.clapHost(), desc->id);

// Initialize and activate
plugin->init(plugin);
plugin->activate(plugin, 48000, 256, 256);
plugin->start_processing(plugin);

// Process audio
StereoAudioBuffers buffers(256);
buffers.fillInputWithSine(440.0f, 48000.0f);

EmptyInputEvents inEvents;
DiscardOutputEvents outEvents;

clap_process_t process{};
process.frames_count = 256;
process.audio_inputs = buffers.inputBuffer();
process.audio_outputs = buffers.outputBuffer();
process.audio_inputs_count = 1;
process.audio_outputs_count = 1;
process.in_events = inEvents.get();
process.out_events = outEvents.get();

plugin->process(plugin, &process);

// Cleanup
plugin->stop_processing(plugin);
plugin->deactivate(plugin);
plugin->destroy(plugin);
```

### Command-Line Validation

```bash
./clap-trap-cli /path/to/plugin.clap
```

This tests:
- Plugin loads successfully
- Factory returns valid descriptors
- Full lifecycle: init → activate → process → deactivate → destroy
- Output contains valid samples (no NaN/Inf)

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Options

- `CLAP_TRAP_BUILD_TESTS`: Build unit tests (default: ON)
- `CLAP_TRAP_BUILD_EXAMPLES`: Build example tools (default: ON)

## Integration with CMake

```cmake
FetchContent_Declare(
    clap-trap
    GIT_REPOSITORY https://github.com/dfl/clap-trap.git
    GIT_TAG v0.1.0
)
FetchContent_MakeAvailable(clap-trap)

target_link_libraries(your-target PRIVATE clap-trap)
```

## How is this different from clap-validator?

[clap-validator](https://github.com/free-audio/clap-validator) is a Rust CLI that checks CLAP spec compliance. It's great. Use it.

**clap-trap** is a C++ library for writing your own tests:
- Test your **host** against plugins
- Test your **bridge** (like [wclap-bridge](https://github.com/WebCLAP/wclap-bridge))
- Test your **plugin loader**
- Write regression tests for specific bugs

If you're a plugin developer, run clap-validator before release. If you're building infrastructure that *loads* plugins, use clap-trap to test it.

## License

MIT

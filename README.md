# clap-trap

[![CI](https://github.com/dfl/clap-trap/actions/workflows/ci.yml/badge.svg)](https://github.com/dfl/clap-trap/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![GitHub release](https://img.shields.io/github/v/release/dfl/clap-trap)](https://github.com/dfl/clap-trap/releases)

A command-line tool for testing CLAP plugins. Validate, benchmark, render audio, and test state—no DAW required.

Supports both **native** `.clap` plugins and **WASM** `.wclap`/`.wasm` plugins (via [wclap-bridge](https://github.com/WebCLAP/wclap-bridge)).

*It's a trap! ...for catching CLAP plugin bugs.*

## Installation

Download binaries from [Releases](https://github.com/dfl/clap-trap/releases)—**WASM plugin support is included by default**.

### Building from Source

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

WASM plugin support is enabled by default ([wclap-bridge](https://github.com/WebCLAP/wclap-bridge) is auto-fetched). For a smaller binary without WASM support:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DCLAP_TRAP_WASM_SUPPORT=OFF
```

## Commands

### validate

Basic smoke test: load plugin, process audio, check for crashes and bad output.

```bash
# Native plugin
clap-trap validate plugin.clap

# WASM plugin bundle
clap-trap validate plugin.wclap

# Raw WASM file
clap-trap validate plugin.wasm
```

```
✓ Plugin loaded
✓ Got plugin factory
✓ Found 1 plugin(s)

── My Plugin ──
  ✓ create_plugin()
  ✓ init()
  ✓ activate(48000 Hz, 256 samples)
  ✓ start_processing()
  ✓ process() x10 blocks
  ✓ stop_processing()
  ✓ deactivate()
  ✓ destroy()

All 1 plugin(s) validated successfully.
```

### info

Dump plugin details: parameters, audio ports, note ports, supported extensions.

```bash
clap-trap info plugin.clap
```

### bench

Measure processing performance.

```bash
clap-trap bench plugin.clap --blocks 10000
```

```
My Plugin                                  3584.2x realtime    7.3 µs/block  (10000 blocks)
```

### process

Offline audio rendering. Process a WAV file through a plugin, or render a synth to WAV.

```bash
# Process audio through an effect
clap-trap process effect.clap -i input.wav -o output.wav

# Render a synth (silence in, capture output)
clap-trap process synth.clap -o output.wav --blocks 1000

# Output as 32-bit float
clap-trap process plugin.clap -i input.wav -o output.wav --float
```

### state

Save/load plugin state, or test state round-trip.

```bash
# Save state to file
clap-trap state plugin.clap -o preset.state

# Load state from file
clap-trap state plugin.clap -i preset.state

# Test round-trip: save, restore, verify all parameters match
clap-trap state plugin.clap --roundtrip
```

```
Plugin: My Plugin
Testing state round-trip...
  Saved state: 1350 bytes
  Captured 31 parameter values
  Restored state
  All 31 parameters match after restore
```

## Options

| Option | Description |
|--------|-------------|
| `--blocks N` | Number of blocks to process |
| `--buffer-size N` | Buffer size in samples (default: 256) |
| `--sample-rate N` | Sample rate in Hz (default: 48000) |
| `-i, --input FILE` | Input WAV file (process) or state file (state) |
| `-o, --output FILE` | Output WAV file (process) or state file (state) |
| `--float` | Output 32-bit float WAV (default: 16-bit PCM) |
| `--roundtrip` | Test state save/load round-trip |

## How is this different from clap-validator?

[clap-validator](https://github.com/free-audio/clap-validator) checks CLAP spec compliance. It's great. Use it.

**clap-trap** is for integration testing:
- Smoke test plugins in CI before release
- Benchmark performance
- Render audio offline for comparison tests
- Verify state save/load works correctly
- **WASM plugin support** (.wclap and .wasm files)

Use clap-validator for spec compliance. Use clap-trap for "does it actually work?"

### Using both in CI

```yaml
# In your plugin's CI workflow
- name: Spec compliance
  run: clap-validator validate my-plugin.clap

- name: Integration tests
  run: |
    clap-trap validate my-plugin.clap
    clap-trap bench my-plugin.clap --blocks 10000
    clap-trap state my-plugin.clap --roundtrip
```

## Advanced: Using as a C++ Library

If you need to write custom tests, clap-trap can be used as a library:

```cmake
FetchContent_Declare(
    clap-trap
    GIT_REPOSITORY https://github.com/dfl/clap-trap.git
    GIT_TAG main
)
FetchContent_MakeAvailable(clap-trap)

target_link_libraries(your-target PRIVATE clap-trap)
```

```cpp
#include "clap-trap/clap-trap.h"

using namespace clap_trap;

auto loader = PluginLoader::load("/path/to/plugin.clap");
const auto* factory = loader->factory();

TestHost host;
const auto* desc = factory->get_plugin_descriptor(factory, 0);
const clap_plugin_t* plugin = factory->create_plugin(factory, host.clapHost(), desc->id);

plugin->init(plugin);
plugin->activate(plugin, 48000, 256, 256);
plugin->start_processing(plugin);

StereoAudioBuffers buffers(256);
buffers.fillInputWithSine(440.0f, 48000.0f);

// ... process audio ...

plugin->stop_processing(plugin);
plugin->deactivate(plugin);
plugin->destroy(plugin);
```

## License

MIT

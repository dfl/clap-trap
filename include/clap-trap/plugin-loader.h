/**
 * clap-trap: Plugin Loader
 *
 * Platform-agnostic CLAP plugin loading via dlopen/LoadLibrary
 * Optional WASM plugin support via wclap-bridge
 */

#pragma once

#include <clap/clap.h>
#include <memory>
#include <string>

#if CLAP_TRAP_HAS_WASM
#include <wclap-bridge.h>
#endif

namespace clap_trap {

/**
 * Loads CLAP plugins from disk.
 *
 * Supports:
 *   - Native .clap plugins (via dlopen/LoadLibrary)
 *   - WASM .wclap/.wasm plugins (via wclap-bridge, if enabled)
 *
 * On macOS: loads .clap bundles
 * On Windows: loads .clap (DLL) files
 * On Linux: loads .clap (shared object) files
 */
class PluginLoader {
public:
    ~PluginLoader();
    PluginLoader(PluginLoader&&) noexcept;
    PluginLoader& operator=(PluginLoader&&) noexcept;

    // Non-copyable
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    /**
     * Create a PluginLoader, auto-detecting plugin type from extension.
     *   - .clap → native loading via dlopen
     *   - .wclap, .wasm → WASM loading via wclap-bridge (if enabled)
     *
     * Returns nullptr on failure - call getError() for details.
     */
    static std::unique_ptr<PluginLoader> create(const std::string& path);

    /**
     * Load a native CLAP plugin from the given path.
     * Returns nullptr on failure - call getError() for details.
     */
    static std::unique_ptr<PluginLoader> load(const std::string& path);

#if CLAP_TRAP_HAS_WASM
    /**
     * Load a WASM CLAP plugin (.wclap or .wasm) via wclap-bridge.
     * Returns nullptr on failure - call getError() for details.
     */
    static std::unique_ptr<PluginLoader> loadWasm(const std::string& path);

    /**
     * Check if WASM support is available at runtime.
     */
    static bool hasWasmSupport() { return true; }
#else
    static bool hasWasmSupport() { return false; }
#endif

    /**
     * Get the plugin entry point (native plugins only).
     * Returns nullptr for WASM plugins or if not loaded.
     */
    const clap_plugin_entry_t* entry() const { return entry_; }

    /**
     * Get the plugin factory (convenience wrapper).
     * Works for both native and WASM plugins.
     * Returns nullptr if not available.
     */
    const clap_plugin_factory_t* factory() const;

    /**
     * Check if this is a WASM plugin.
     */
    bool isWasm() const {
#if CLAP_TRAP_HAS_WASM
        return wclap_ != nullptr;
#else
        return false;
#endif
    }

    /**
     * Get the last error message.
     */
    const std::string& getError() const { return error_; }

    /**
     * Get the loaded plugin path.
     */
    const std::string& path() const { return path_; }

private:
    PluginLoader() = default;

    void* handle_ = nullptr;
#if CLAP_TRAP_HAS_WASM
    void* wclap_ = nullptr;
#endif
    const clap_plugin_entry_t* entry_ = nullptr;
    std::string path_;
    std::string error_;
    bool initialized_ = false;
};

} // namespace clap_trap


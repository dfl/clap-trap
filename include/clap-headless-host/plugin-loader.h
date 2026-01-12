/**
 * clap-headless-host: Plugin Loader
 *
 * Platform-agnostic CLAP plugin loading via dlopen/LoadLibrary
 */

#pragma once

#include <clap/clap.h>
#include <memory>
#include <string>

namespace clap_headless {

/**
 * Loads a native CLAP plugin from disk.
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
     * Load a CLAP plugin from the given path.
     * Returns nullptr on failure - call getError() for details.
     */
    static std::unique_ptr<PluginLoader> load(const std::string& path);

    /**
     * Get the plugin entry point.
     * Returns nullptr if not loaded or init failed.
     */
    const clap_plugin_entry_t* entry() const { return entry_; }

    /**
     * Get the plugin factory (convenience wrapper).
     * Returns nullptr if not available.
     */
    const clap_plugin_factory_t* factory() const;

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
    const clap_plugin_entry_t* entry_ = nullptr;
    std::string path_;
    std::string error_;
    bool initialized_ = false;
};

} // namespace clap_headless

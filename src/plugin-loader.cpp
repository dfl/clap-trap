/**
 * clap-trap: Plugin Loader Implementation
 *
 * Cross-platform CLAP plugin loading via dlopen/LoadLibrary
 */

#include "clap-trap/plugin-loader.h"
#include <cstring>

#include "wclap-bridge/wclap-bridge.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace clap_trap {

PluginLoader::~PluginLoader() {
    if (initialized_ && entry_) {
        entry_->deinit();
    }

    if (handle_) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
    } else if (wclap_) {
        wclap_close(wclap_);
    }
}

PluginLoader::PluginLoader(PluginLoader&& other) noexcept
    : handle_(other.handle_)
    , wclap_(other.wclap_)
    , entry_(other.entry_)
    , path_(std::move(other.path_))
    , error_(std::move(other.error_))
    , initialized_(other.initialized_) {
    other.handle_ = nullptr;
    other.wclap_ = nullptr;
    other.entry_ = nullptr;
    other.initialized_ = false;
}

PluginLoader& PluginLoader::operator=(PluginLoader&& other) noexcept {
    if (this != &other) {
        // Clean up existing
        if (initialized_ && entry_) {
            entry_->deinit();
        }
        if (handle_) {
#if defined(_WIN32)
            FreeLibrary(static_cast<HMODULE>(handle_));
#else
            dlclose(handle_);
#endif
        } else if (wclap_) {
            wclap_close(wclap_);
        }

        // Move from other
        handle_ = other.handle_;
        wclap_ = other.wclap_;
        entry_ = other.entry_;
        path_ = std::move(other.path_);
        error_ = std::move(other.error_);
        initialized_ = other.initialized_;

        other.handle_ = nullptr;
        other.wclap_ = nullptr;
        other.entry_ = nullptr;
        other.initialized_ = false;
    }
    return *this;
}

std::unique_ptr<PluginLoader> PluginLoader::load(const std::string& path) {
    auto loader = std::unique_ptr<PluginLoader>(new PluginLoader());
    loader->path_ = path;

    std::string loadPath = path;

#if defined(__APPLE__)
    // On macOS, .clap files are bundles - we need to load the actual binary inside
    // Check if path ends with .clap and append Contents/MacOS/<name>
    if (path.size() > 5 && path.substr(path.size() - 5) == ".clap") {
        // Extract bundle name
        size_t lastSlash = path.rfind('/');
        std::string bundleName;
        if (lastSlash != std::string::npos) {
            bundleName = path.substr(lastSlash + 1);
        } else {
            bundleName = path;
        }
        // Remove .clap extension
        bundleName = bundleName.substr(0, bundleName.size() - 5);
        loadPath = path + "/Contents/MacOS/" + bundleName;
    }
#endif

    // Load the library
#if defined(_WIN32)
    loader->handle_ = LoadLibraryA(loadPath.c_str());
    if (!loader->handle_) {
        loader->error_ = "Failed to load library: " + std::to_string(GetLastError());
        return loader;
    }
#else
    loader->handle_ = dlopen(loadPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!loader->handle_) {
        const char* err = dlerror();
        loader->error_ = "Failed to load library: ";
        loader->error_ += err ? err : "unknown error";
        return loader;
    }
#endif

    // Get the entry point
#if defined(_WIN32)
    loader->entry_ = reinterpret_cast<const clap_plugin_entry_t*>(
        GetProcAddress(static_cast<HMODULE>(loader->handle_), "clap_entry"));
#else
    loader->entry_ = reinterpret_cast<const clap_plugin_entry_t*>(
        dlsym(loader->handle_, "clap_entry"));
#endif

    if (!loader->entry_) {
        loader->error_ = "Failed to find clap_entry symbol";
        return loader;
    }

    // Verify CLAP version compatibility
    if (!clap_version_is_compatible(loader->entry_->clap_version)) {
        loader->error_ = "Incompatible CLAP version";
        return loader;
    }

    // Initialize the plugin
    if (!loader->entry_->init(path.c_str())) {
        loader->error_ = "clap_entry->init() returned false";
        loader->entry_ = nullptr;
        return loader;
    }

    loader->initialized_ = true;
    return loader;
}

std::unique_ptr<PluginLoader> PluginLoader::loadWclap(const std::string& path) {
    auto loader = std::unique_ptr<PluginLoader>(new PluginLoader());
    loader->path_ = path;

    loader->wclap_ = wclap_open(path);

    char errorMessage[256] = {};
    if (wclap_get_error(loader->wclap_, errorMessage, 255)) {
        loader->error_ = errorMessage;
    }
}
    
const clap_plugin_factory_t* PluginLoader::factory() const {
    if (wclap_) return static_cast<const clap_plugin_factory_t*>(
        wclap_get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!entry_) return nullptr;
    return static_cast<const clap_plugin_factory_t*>(
        entry_->get_factory(CLAP_PLUGIN_FACTORY_ID));
}

} // namespace clap_trap

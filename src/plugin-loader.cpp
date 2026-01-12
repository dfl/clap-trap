/**
 * clap-trap: Plugin Loader Implementation
 *
 * Cross-platform CLAP plugin loading via dlopen/LoadLibrary
 * Optional WASM plugin support via wclap-bridge
 */

#include "clap-trap/plugin-loader.h"
#include <cstring>

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

// Helper to check file extension (case-insensitive)
static bool hasExtension(const std::string &path, const std::string &ext) {
  if (path.size() < ext.size())
    return false;
  std::string pathExt = path.substr(path.size() - ext.size());
  for (auto &c : pathExt)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return pathExt == ext;
}

PluginLoader::~PluginLoader() {
#if CLAP_TRAP_HAS_WASM
  if (wclap_) {
    wclap_close(wclap_);
  } else
#endif
  {
    if (initialized_ && entry_) {
      entry_->deinit();
    }
    if (handle_) {
#if defined(_WIN32)
      FreeLibrary(static_cast<HMODULE>(handle_));
#else
      dlclose(handle_);
#endif
    }
  }
}

PluginLoader::PluginLoader(PluginLoader &&other) noexcept
    : handle_(other.handle_)
#if CLAP_TRAP_HAS_WASM
      ,
      wclap_(other.wclap_)
#endif
      ,
      entry_(other.entry_), path_(std::move(other.path_)),
      error_(std::move(other.error_)), initialized_(other.initialized_) {
  other.handle_ = nullptr;
#if CLAP_TRAP_HAS_WASM
  other.wclap_ = nullptr;
#endif
  other.entry_ = nullptr;
  other.initialized_ = false;
}

PluginLoader &PluginLoader::operator=(PluginLoader &&other) noexcept {
  if (this != &other) {
    // Clean up existing
#if CLAP_TRAP_HAS_WASM
    if (wclap_) {
      wclap_close(wclap_);
    } else
#endif
    {
      if (initialized_ && entry_) {
        entry_->deinit();
      }
      if (handle_) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
      }
    }

    // Move from other
    handle_ = other.handle_;
#if CLAP_TRAP_HAS_WASM
    wclap_ = other.wclap_;
#endif
    entry_ = other.entry_;
    path_ = std::move(other.path_);
    error_ = std::move(other.error_);
    initialized_ = other.initialized_;

    other.handle_ = nullptr;
#if CLAP_TRAP_HAS_WASM
    other.wclap_ = nullptr;
#endif
    other.entry_ = nullptr;
    other.initialized_ = false;
  }
  return *this;
}

std::unique_ptr<PluginLoader> PluginLoader::create(const std::string &path) {
  // Auto-detect plugin type based on extension
  if (hasExtension(path, ".wclap") || hasExtension(path, ".wasm")) {
#if CLAP_TRAP_HAS_WASM
    return loadWasm(path);
#else
    auto loader = std::unique_ptr<PluginLoader>(new PluginLoader());
    loader->path_ = path;
    loader->error_ =
        "WASM support not enabled. Rebuild with -DCLAP_TRAP_WASM_SUPPORT=ON";
    return loader;
#endif
  }
  return load(path);
}

std::unique_ptr<PluginLoader> PluginLoader::load(const std::string &path) {
  auto loader = std::unique_ptr<PluginLoader>(new PluginLoader());
  loader->path_ = path;

  std::string loadPath = path;

#if defined(__APPLE__)
  // On macOS, .clap files are bundles - we need to load the actual binary
  // inside Check if path ends with .clap and append Contents/MacOS/<name>
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
    loader->error_ =
        "Failed to load library: " + std::to_string(GetLastError());
    return loader;
  }
#else
  loader->handle_ = dlopen(loadPath.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!loader->handle_) {
    const char *err = dlerror();
    loader->error_ = "Failed to load library: ";
    loader->error_ += err ? err : "unknown error";
    return loader;
  }
#endif

  // Get the entry point
#if defined(_WIN32)
  loader->entry_ = reinterpret_cast<const clap_plugin_entry_t *>(
      GetProcAddress(static_cast<HMODULE>(loader->handle_), "clap_entry"));
#else
  loader->entry_ = reinterpret_cast<const clap_plugin_entry_t *>(
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

#if CLAP_TRAP_HAS_WASM
std::unique_ptr<PluginLoader> PluginLoader::loadWasm(const std::string &path) {
  auto loader = std::unique_ptr<PluginLoader>(new PluginLoader());
  loader->path_ = path;

  // Initialize WASM runtime if needed (0 = no timeout)
  static bool globalInitDone = false;
  if (!globalInitDone) {
    if (!wclap_global_init(0)) {
      loader->error_ = "Failed to initialize WASM runtime";
      return loader;
    }
    globalInitDone = true;
  }

  // Open the WASM plugin
  loader->wclap_ = wclap_open(path.c_str());
  if (!loader->wclap_) {
    loader->error_ = "Failed to open WASM plugin";
    return loader;
  }

  // Check for errors
  char errorBuffer[256] = {};
  if (wclap_get_error(loader->wclap_, errorBuffer, sizeof(errorBuffer) - 1)) {
    loader->error_ = errorBuffer;
    return loader;
  }

  // Verify we can get a factory
  if (!loader->factory()) {
    loader->error_ = "WASM plugin does not provide a plugin factory";
    return loader;
  }

  loader->initialized_ = true;
  return loader;
}
#endif

const clap_plugin_factory_t *PluginLoader::factory() const {
#if CLAP_TRAP_HAS_WASM
  if (wclap_) {
    return static_cast<const clap_plugin_factory_t *>(
        wclap_get_factory(wclap_, CLAP_PLUGIN_FACTORY_ID));
  }
#endif
  if (!entry_)
    return nullptr;
  return static_cast<const clap_plugin_factory_t *>(
      entry_->get_factory(CLAP_PLUGIN_FACTORY_ID));
}

} // namespace clap_trap

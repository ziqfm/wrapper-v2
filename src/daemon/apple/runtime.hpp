// Apple Music native runtime initialization.
//
// `Runtime::initialize()` runs the equivalent of upstream main.c's
// `init()` and `init_ctx()` in one go. After it returns true, the
// process holds a configured `RequestContext` shared_ptr that
// downstream Apple-API calls (storefront lookup, m3u8, decrypt) need.
//
// Phase 1.0 calls this from main() only. Phase 1.1+ will use the
// stored RequestContext for actual API calls. The runtime is
// process-wide and single-instance: Apple's libs use process-global
// state and are not safe to re-initialize.

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "apple/abi.hpp"
#include "apple/loader.hpp"

namespace wrapper::apple {

// Configuration sourced from environment variables. See
// .env.example for defaults.
struct RuntimeConfig {
    // Filesystem path Apple's libs will use for FairPlay key cache,
    // mpl_db, etc. Mapped to upstream's --base-dir / -B flag.
    std::string base_dir = "/data/data/com.apple.android.music/files";

    // Slash-separated 9-tuple identifying the fake "Apple Music
    // Android client" we present to Apple's servers. Order:
    //   ClientIdentifier / VersionIdentifier / PlatformIdentifier /
    //   ProductVersion   / DeviceModel       / BuildVersion       /
    //   LocaleIdentifier / LanguageIdentifier / AndroidID
    std::string device_info =
        "Music/4.9/Android/10/Samsung S9/7663313/en-US/en-US/dc28071e981c439e";
};

// Singleton: Apple's libs share process-global state, so only one
// Runtime instance per process is ever valid.
class Runtime {
public:
    static Runtime& instance();

    // Idempotent: subsequent calls after the first successful one
    // are no-ops returning true. On failure the runtime stays
    // uninitialized and subsequent calls will retry. Errors are
    // logged to stderr with a `runtime: ` prefix. The Loader must
    // remain alive for the lifetime of the process.
    bool initialize(const Loader& loader, const RuntimeConfig& cfg);

    bool initialized() const { return initialized_.load(std::memory_order_acquire); }

    // Snapshot of the configuration that was used for initialize().
    // Empty string if not initialized yet.
    std::string base_dir() const;
    std::string device_info() const;

private:
    Runtime() = default;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    bool init_dns_and_foothill(const Symbols& s,
                               const std::vector<std::string>& parts);
    bool init_request_context(const Symbols& s,
                              const RuntimeConfig& cfg,
                              const std::vector<std::string>& parts);

    mutable std::mutex mu_;
    std::atomic<bool> initialized_{false};
    abi::shared_ptr request_ctx_{};
    abi::shared_ptr device_guid_{};
    std::string base_dir_;
    std::string device_info_;
};

}  // namespace wrapper::apple

// Port of upstream main.c::init() + init_ctx() to C++.
//
// Behavioral diffs vs. upstream:
//   - We do NOT install a presentation interface (no dialog/credential
//     handlers). Phase 1.0 never triggers AuthenticateFlow::run, so
//     these slots are never called.
//   - We do NOT spin up SVPlaybackLeaseManager. That's a decryption
//     prerequisite and lives in Phase 1.4.
//   - We log to stderr with a `runtime:` prefix instead of the
//     upstream `[+] ...` prefix.
//
// All Apple calls are unchecked: there are no error returns to
// validate. If an Apple call crashes mid-init, we want the process to
// die rather than mask the failure with a half-initialized
// RequestContext.

#include "apple/runtime.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace wrapper::apple {

namespace {

constexpr int kDeviceInfoPartCount = 9;

// Split "a/b/c/d/.../i" into 9 string parts. Returns empty vector
// if the count doesn't match (the runtime considers that a hard
// error: Apple's libs assume all 9 fields are present).
std::vector<std::string> split_device_info(const std::string& s) {
    std::vector<std::string> out;
    out.reserve(kDeviceInfoPartCount);

    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, '/')) {
        out.push_back(std::move(part));
    }
    if (static_cast<int>(out.size()) != kDeviceInfoPartCount) {
        out.clear();
    }
    return out;
}

}  // namespace

Runtime& Runtime::instance() {
    static Runtime r;
    return r;
}

bool Runtime::initialize(const Loader& loader, const RuntimeConfig& cfg) {
    std::lock_guard<std::mutex> g(mu_);
    if (initialized_.load(std::memory_order_relaxed)) return true;

    if (!loader.ok()) {
        std::fprintf(stderr,
                     "runtime: cannot initialize, loader is not ok (last_error=%s)\n",
                     loader.last_error().c_str());
        return false;
    }

    auto parts = split_device_info(cfg.device_info);
    if (parts.empty()) {
        std::fprintf(stderr,
                     "runtime: WRAPPER_DEVICE_INFO must have %d slash-separated "
                     "parts, got %zu (raw=%.200s)\n",
                     kDeviceInfoPartCount,
                     std::count(cfg.device_info.begin(), cfg.device_info.end(), '/') + 1,
                     cfg.device_info.c_str());
        return false;
    }

    std::fprintf(stderr, "runtime: starting Apple lib init (base_dir=%s)\n",
                 cfg.base_dir.c_str());

    const Symbols& s = loader.sym();
    if (!init_dns_and_foothill(s, parts)) return false;
    if (!init_request_context(s, cfg, parts)) return false;

    base_dir_ = cfg.base_dir;
    device_info_ = cfg.device_info;
    initialized_.store(true, std::memory_order_release);

    std::fprintf(stderr, "runtime: ready\n");
    return true;
}

bool Runtime::init_dns_and_foothill(const Symbols& s,
                                    const std::vector<std::string>& parts) {
    // Bionic's resolver inside the chroot has no /etc/resolv.conf to
    // read. Set Alibaba's public DNS so the bundled libcurl can
    // reach api.itunes.apple.com etc. Upstream uses the same hosts.
    static const char* resolvers[2] = {"223.5.5.5", "223.6.6.6"};
    s.resolv_set_nameservers_for_net(0, resolvers, 2, ".");

    // FootHillConfig::config(android_id). Android ID is part 8 of
    // the device-info tuple.
    auto android_id = abi::make_string_view(parts[8].c_str());
    s.FootHillConfig_config(&android_id);

    // DeviceGUID::instance() -> shared_ptr<DeviceGUID>
    s.DeviceGUID_instance(&device_guid_);
    if (device_guid_.obj == nullptr) {
        std::fprintf(stderr, "runtime: DeviceGUID::instance returned null\n");
        return false;
    }

    // DeviceGUID::configure(android_id, "", 29, true). The first arg
    // is the hidden return slot for the (88-byte) Configuration<>
    // value Apple discards.
    static std::uint8_t cfg_ret[88];
    static const unsigned int cfg_kind = 29;
    static const std::uint8_t cfg_flag = 1;
    auto empty = abi::make_string_view("");
    s.DeviceGUID_configure(&cfg_ret, device_guid_.obj,
                           &android_id, &empty, &cfg_kind, &cfg_flag);

    return true;
}

bool Runtime::init_request_context(const Symbols& s,
                                   const RuntimeConfig& cfg,
                                   const std::vector<std::string>& parts) {
    // make_shared<RequestContext>(mpl_db_path).
    std::string mpl_db = cfg.base_dir + "/mpl_db";
    auto mpl_db_view = abi::make_string_view(mpl_db.c_str());
    s.make_shared_RequestContext(&request_ctx_, &mpl_db_view);

    if (request_ctx_.obj == nullptr) {
        std::fprintf(stderr, "runtime: make_shared<RequestContext> returned null\n");
        return false;
    }

    // RequestContextConfig: emplaced in a stack-ish 480-byte buffer
    // with a hand-rolled vtable pointer for the shared_ptr control
    // block. Pattern lifted verbatim from upstream init_ctx().
    static std::uint8_t rcc_buf[480];
    *reinterpret_cast<void**>(rcc_buf) =
        s.vtable_RequestContextConfig + 2;  // skip past vtable header (type_info + dtor slots)

    abi::shared_ptr rcc;
    rcc.obj = rcc_buf + 32;
    rcc.ctrl_blk = rcc_buf;

    s.RequestContextConfig_ctor(rcc.obj);

    // Setters: order matches upstream init_ctx().
    s.RCC_setBaseDirectoryPath(rcc.obj, &mpl_db_view);

    auto s0 = abi::make_string_view(parts[0].c_str());
    s.RCC_setClientIdentifier(rcc.obj, &s0);

    auto s1 = abi::make_string_view(parts[1].c_str());
    s.RCC_setVersionIdentifier(rcc.obj, &s1);

    auto s2 = abi::make_string_view(parts[2].c_str());
    s.RCC_setPlatformIdentifier(rcc.obj, &s2);

    auto s3 = abi::make_string_view(parts[3].c_str());
    s.RCC_setProductVersion(rcc.obj, &s3);

    auto s4 = abi::make_string_view(parts[4].c_str());
    s.RCC_setDeviceModel(rcc.obj, &s4);

    auto s5 = abi::make_string_view(parts[5].c_str());
    s.RCC_setBuildVersion(rcc.obj, &s5);

    auto s6 = abi::make_string_view(parts[6].c_str());
    s.RCC_setLocaleIdentifier(rcc.obj, &s6);

    auto s7 = abi::make_string_view(parts[7].c_str());
    s.RCC_setLanguageIdentifier(rcc.obj, &s7);

    // Wire the RequestContext into the singleton manager.
    s.RequestContextManager_configure(&request_ctx_);

    // RequestContext::init(rcc). Upstream uses an 88-byte hidden
    // return slot - match it.
    static std::uint8_t rci_ret[88];
    s.RequestContext_init(&rci_ret, request_ctx_.obj, &rcc);

    auto fp_dir = abi::make_string_view(cfg.base_dir.c_str());
    s.RequestContext_setFairPlayDirectoryPath(request_ctx_.obj, &fp_dir);

    return true;
}

std::string Runtime::base_dir() const {
    if (!initialized_.load(std::memory_order_acquire)) return {};
    std::lock_guard<std::mutex> g(mu_);
    return base_dir_;
}

std::string Runtime::device_info() const {
    if (!initialized_.load(std::memory_order_acquire)) return {};
    std::lock_guard<std::mutex> g(mu_);
    return device_info_;
}

}  // namespace wrapper::apple

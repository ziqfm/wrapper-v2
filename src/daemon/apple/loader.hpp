// Apple Music native-library loader.
//
// `Loader::open()` dlopens libstoreservicescore.so + libmediaplatform.so +
// libandroidappmusic.so (whose DT_NEEDED chains pull in the full Android
// system-library closure), then resolves every mangled symbol declared
// in apple/abi.hpp into a function-pointer table held in `Symbols`.
//
// The daemon ELF itself has *no* DT_NEEDED reference to Apple's libs -
// this is what lets us start the daemon in stub mode even when the libs
// have not been staged. If the libs are missing or a symbol fails to
// resolve, `open()` returns false and the runtime stays unavailable.

#pragma once

#include <string>

#include "apple/abi.hpp"

namespace wrapper::apple {

// Function-pointer table populated by Loader::open(). All fields are
// non-null after a successful open(); read access while ok()==false
// is undefined.
struct Symbols {
    // Vtable for std::__shared_ptr_emplace<RequestContextConfig, ...>.
    // Stored as `void**` so callers can do pointer arithmetic the
    // same way upstream main.c does (skip past type_info + dtor).
    void** vtable_RequestContextConfig = nullptr;

    abi::fn_resolv_set_nameservers_for_net     resolv_set_nameservers_for_net = nullptr;
    abi::fn_FootHillConfig_config              FootHillConfig_config          = nullptr;
    abi::fn_DeviceGUID_instance                DeviceGUID_instance            = nullptr;
    abi::fn_DeviceGUID_configure               DeviceGUID_configure           = nullptr;
    abi::fn_make_shared_RequestContext         make_shared_RequestContext     = nullptr;
    abi::fn_RequestContextConfig_ctor          RequestContextConfig_ctor      = nullptr;

    abi::fn_RCC_set_string RCC_setBaseDirectoryPath  = nullptr;
    abi::fn_RCC_set_string RCC_setClientIdentifier   = nullptr;
    abi::fn_RCC_set_string RCC_setVersionIdentifier  = nullptr;
    abi::fn_RCC_set_string RCC_setPlatformIdentifier = nullptr;
    abi::fn_RCC_set_string RCC_setProductVersion     = nullptr;
    abi::fn_RCC_set_string RCC_setDeviceModel        = nullptr;
    abi::fn_RCC_set_string RCC_setBuildVersion       = nullptr;
    abi::fn_RCC_set_string RCC_setLocaleIdentifier   = nullptr;
    abi::fn_RCC_set_string RCC_setLanguageIdentifier = nullptr;

    abi::fn_RequestContextManager_configure RequestContextManager_configure = nullptr;
    abi::fn_RequestContext_init             RequestContext_init             = nullptr;
    abi::fn_RCC_set_string                  RequestContext_setFairPlayDirectoryPath = nullptr;
};

class Loader {
public:
    Loader() = default;

    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;

    // Try to dlopen Apple's libs from `libs_dir` (typically /system/lib64
    // inside the chroot). On success, resolves all symbols into the
    // internal Symbols table and returns true. On failure, leaves the
    // loader in a closed state and returns false; subsequent ok() calls
    // return false.
    bool open(const std::string& libs_dir);

    // Release all dlopen handles. Symbols become invalid.
    void close();

    bool ok() const { return ok_; }

    const Symbols& sym() const { return symbols_; }

    // Last failure description (path of the lib, symbol name, dlerror()).
    // Empty if no failure has been recorded.
    const std::string& last_error() const { return last_error_; }

private:
    void* h_libstoreservicescore_ = nullptr;
    void* h_libmediaplatform_     = nullptr;
    void* h_libandroidappmusic_   = nullptr;

    Symbols symbols_;
    bool ok_ = false;
    std::string last_error_;
};

}  // namespace wrapper::apple

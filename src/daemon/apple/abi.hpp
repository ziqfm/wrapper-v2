// Apple Music native-lib ABI bindings (x86_64, APK 3.6.0-beta).
//
// We call into Apple's libstoreservicescore / libmediaplatform /
// libandroidappmusic by way of the Itanium-mangled C++ symbols they
// export. Each symbol below is declared as a function pointer type;
// the Loader class (apple/loader.hpp) dlopens the libs at runtime
// and resolves the symbols via dlsym.
//
// We deliberately treat all Apple types as opaque (`void*`) and pass
// strings as pointers to a layout-compatible `std_string` union. This
// is the pattern used by the upstream `wrapper` project; it avoids
// compile-time inclusion of Apple headers we don't have, at the cost
// of giving up type safety.
//
// Stability: the symbol names below are pinned to APK build 1109. If
// the APK is changed, re-derive with `nm -D --defined libstoreservicescore.so`
// and update LIBS_VERSION.json.
//
// Phase 1.0 only declares the symbols we need to bring the runtime up
// (DNS, DeviceGUID, RequestContext, FootHillConfig). Auth, m3u8, and
// decrypt symbols will land in subsequent commits as they are wired
// up.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wrapper::apple::abi {

// libc++ on the Android NDK uses an inline namespace `__ndk1`, which
// shows up in mangled names. The C++ standard library types we touch
// (`std::shared_ptr`, `std::basic_string`, `std::vector`) have a
// well-known POD-ish layout we can fake without including <memory>
// etc. Mismatching this layout WILL miscompile silently; tread
// carefully.

// std::__ndk1::shared_ptr<T> on x86_64: { T* obj; control_block* ctl; }.
// Total size 16 bytes. Apple methods returning shared_ptr take a
// hidden first arg pointing at one of these.
struct shared_ptr {
    void* obj = nullptr;
    void* ctrl_blk = nullptr;
};

// std::__ndk1::basic_string<char> on x86_64 uses the libc++ "long" /
// "short" union layout. Size: 24 bytes. Short mode uses the low bit
// of the first byte as the tag (0 = short). Long mode stores
// {capacity, size, data_ptr}.
union std_string {
    struct {
        std::uint8_t mark;     // bit 0 set => long mode
        char str[23];          // SSO buffer (22 chars + NUL)
    } _short;
    struct {
        std::size_t cap;       // capacity (with bit 0 = 1 => long)
        std::size_t size;      // length
        const char* data;      // pointer to heap-allocated storage
    } _long;
};

static_assert(sizeof(std_string) == 24, "std_string ABI mismatch");
static_assert(sizeof(shared_ptr) == 16, "shared_ptr ABI mismatch");

// Pass a C string to an Apple method by reference. We deliberately do
// NOT take ownership of `s`: the Apple call reads the pointer
// directly. The caller must keep the underlying buffer live for the
// duration of the call.
inline std_string make_string_view(const char* s) {
    std_string out{};
    out._long.cap = 1;                    // any odd value => long mode
    out._long.size = std::strlen(s);
    out._long.data = s;
    return out;
}

// Empty std::vector<T> on x86_64: three null pointers (begin/end/capacity).
struct std_vector {
    void* begin = nullptr;
    void* end = nullptr;
    void* end_capacity = nullptr;
};

// ---------------------------------------------------------------------------
// Function pointer types for runtime-init symbols (Phase 1.0)
// ---------------------------------------------------------------------------
//
// `using fn_X = void (*)(...);` rather than direct extern "C" decls.
// This keeps the daemon ELF free of unresolved Apple symbols at link
// time so it can be started without the Apple libs available - useful
// for stub-mode operation (WRAPPER_APPLE_INIT=0).

// bionic: void _resolv_set_nameservers_for_net(unsigned, const char**, int, const char*)
using fn_resolv_set_nameservers_for_net =
    void (*)(unsigned netid, const char** servers, int count, const char* domains);

// FootHillConfig::config(std::string const&)
using fn_FootHillConfig_config = void (*)(std_string* device_id);

// storeservicescore::DeviceGUID::instance() -> shared_ptr<DeviceGUID>
using fn_DeviceGUID_instance = void (*)(shared_ptr* out);

// storeservicescore::DeviceGUID::configure(std::string const&,
//                                          std::string const&,
//                                          unsigned int const&,
//                                          bool const&)
using fn_DeviceGUID_configure =
    void (*)(void* hidden_return,
             void* this_,
             std_string* arg1,
             std_string* arg2,
             const unsigned int* arg3,
             const std::uint8_t* arg4);

// std::shared_ptr<RequestContext>::make_shared<std::string&>(std::string&)
using fn_make_shared_RequestContext =
    void (*)(shared_ptr* out, std_string* arg);

// storeservicescore::RequestContextConfig::RequestContextConfig()
using fn_RequestContextConfig_ctor = void (*)(void* this_);

// All RequestContextConfig string setters have the same signature.
using fn_RCC_set_string = void (*)(void* this_, std_string* s);

// RequestContextManager::configure(shared_ptr<RequestContext> const&)
using fn_RequestContextManager_configure = void (*)(shared_ptr* req_ctx);

// storeservicescore::RequestContext::init(shared_ptr<RequestContextConfig> const&)
using fn_RequestContext_init =
    void (*)(void* hidden_return, void* this_, shared_ptr* config);

// Mangled symbol names. These are the *strings* we feed to dlsym.
// Kept centrally so that the Loader implementation does not embed
// them inline (easier to audit and regenerate from `nm` output).
namespace mangled {

inline constexpr const char* resolv_set_nameservers_for_net =
    "_resolv_set_nameservers_for_net";

inline constexpr const char* FootHillConfig_config =
    "_ZN14FootHillConfig6configERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEE";

inline constexpr const char* DeviceGUID_instance =
    "_ZN17storeservicescore10DeviceGUID8instanceEv";

inline constexpr const char* DeviceGUID_configure =
    "_ZN17storeservicescore10DeviceGUID9configureERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_RKjRKb";

inline constexpr const char* make_shared_RequestContext =
    "_ZNSt6__ndk110shared_ptrIN17storeservicescore14RequestContextEE11make_sharedIJRNS_12basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEEEEES3_DpOT_";

inline constexpr const char* vtable_RequestContextConfig =
    "_ZTVNSt6__ndk120__shared_ptr_emplaceIN17storeservicescore20RequestContextConfigENS_9allocatorIS2_EEEE";

inline constexpr const char* RequestContextConfig_ctor =
    "_ZN17storeservicescore20RequestContextConfigC2Ev";

inline constexpr const char* RCC_setBaseDirectoryPath =
    "_ZN17storeservicescore20RequestContextConfig20setBaseDirectoryPathERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";
inline constexpr const char* RCC_setClientIdentifier =
    "_ZN17storeservicescore20RequestContextConfig19setClientIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";
inline constexpr const char* RCC_setVersionIdentifier =
    "_ZN17storeservicescore20RequestContextConfig20setVersionIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";
inline constexpr const char* RCC_setPlatformIdentifier =
    "_ZN17storeservicescore20RequestContextConfig21setPlatformIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";
inline constexpr const char* RCC_setProductVersion =
    "_ZN17storeservicescore20RequestContextConfig17setProductVersionERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";
inline constexpr const char* RCC_setDeviceModel =
    "_ZN17storeservicescore20RequestContextConfig14setDeviceModelERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";
inline constexpr const char* RCC_setBuildVersion =
    "_ZN17storeservicescore20RequestContextConfig15setBuildVersionERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";
inline constexpr const char* RCC_setLocaleIdentifier =
    "_ZN17storeservicescore20RequestContextConfig19setLocaleIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";
inline constexpr const char* RCC_setLanguageIdentifier =
    "_ZN17storeservicescore20RequestContextConfig21setLanguageIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";

inline constexpr const char* RequestContextManager_configure =
    "_ZN21RequestContextManager9configureERKNSt6__ndk110shared_ptrIN17storeservicescore14RequestContextEEE";
inline constexpr const char* RequestContext_init =
    "_ZN17storeservicescore14RequestContext4initERKNSt6__ndk110shared_ptrINS_20RequestContextConfigEEE";
inline constexpr const char* RequestContext_setFairPlayDirectoryPath =
    "_ZN17storeservicescore14RequestContext24setFairPlayDirectoryPathERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";

}  // namespace mangled

}  // namespace wrapper::apple::abi

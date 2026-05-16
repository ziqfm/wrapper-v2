// wrapper-v2 daemon entry point.
//
// Phase 1.0 wires up the Apple-lib runtime (DeviceGUID, RequestContext,
// FootHillConfig) at startup, plus a tiny HTTP API for managing the
// stored Media User Token:
//
//   GET  /health
//   GET  /me
//   POST /login        body: { "media_user_token": "..." }
//   DELETE /login
//
// Configuration is environment-only (no command-line flags beyond
// --help). All knobs use the WRAPPER_ prefix:
//
//   WRAPPER_HOST          Bind address (default 0.0.0.0)
//   WRAPPER_PORT          Bind port    (default 80)
//   WRAPPER_BASE_DIR      Apple-lib working dir (default
//                         /data/data/com.apple.android.music/files)
//   WRAPPER_DEVICE_INFO   9-tuple device identifier
//   WRAPPER_APPLE_INIT    "0" to skip Apple lib init at startup
//                         (useful for /health-only smoke tests).

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <httplib.h>

#include "apple/auth.hpp"
#include "apple/loader.hpp"
#include "apple/runtime.hpp"
#include "server.hpp"

namespace {

constexpr const char* kDefaultHost    = "0.0.0.0";
constexpr int         kDefaultPort    = 80;
constexpr const char* kVersion        = "0.1.0-phase1";

std::atomic<httplib::Server*> g_server{nullptr};

void on_signal(int sig) {
    auto* s = g_server.load();
    if (s != nullptr) {
        std::fprintf(stderr, "wrapper-v2: caught signal %d, stopping server\n", sig);
        s->stop();
    }
}

std::string env_or(const char* name, std::string fallback) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return fallback;
    return std::string(v);
}

bool env_bool(const char* name, bool fallback) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return fallback;
    if (std::strcmp(v, "0") == 0 || std::strcmp(v, "false") == 0
        || std::strcmp(v, "no") == 0) {
        return false;
    }
    return true;
}

struct Args {
    std::string host;
    int port;
};

Args parse_args(int argc, char** argv) {
    Args a;
    a.host = env_or("WRAPPER_HOST", kDefaultHost);
    a.port = std::atoi(env_or("WRAPPER_PORT", std::to_string(kDefaultPort)).c_str());
    if (a.port <= 0) a.port = kDefaultPort;

    for (int i = 1; i < argc; ++i) {
        std::string_view x = argv[i];
        if ((x == "--host" || x == "-H") && i + 1 < argc) {
            a.host = argv[++i];
        } else if ((x == "--port" || x == "-p") && i + 1 < argc) {
            a.port = std::atoi(argv[++i]);
        } else if (x == "--help" || x == "-h") {
            std::printf(
                "wrapper-v2 daemon (%s)\n"
                "Usage: %s [--host HOST] [--port PORT]\n"
                "\n"
                "Environment:\n"
                "  WRAPPER_HOST          bind address (default %s)\n"
                "  WRAPPER_PORT          bind port    (default %d)\n"
                "  WRAPPER_BASE_DIR      Apple-lib working dir\n"
                "  WRAPPER_DEVICE_INFO   9-tuple device identifier\n"
                "  WRAPPER_APPLE_INIT    set to 0 to skip Apple lib init\n",
                kVersion, argv[0], kDefaultHost, kDefaultPort);
            std::exit(0);
        }
    }
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    auto args = parse_args(argc, argv);

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);

    wrapper::ServerInfo info;
    info.version = kVersion;
    info.apple_init_enabled = env_bool("WRAPPER_APPLE_INIT", true);

    wrapper::apple::AuthState auth;
    wrapper::apple::Loader   loader;
    auto& runtime = wrapper::apple::Runtime::instance();

    std::string libs_dir = env_or("WRAPPER_LIBS_DIR", "/system/lib64");

    if (info.apple_init_enabled) {
        if (loader.open(libs_dir)) {
            wrapper::apple::RuntimeConfig rcfg;
            rcfg.base_dir    = env_or("WRAPPER_BASE_DIR",    rcfg.base_dir);
            rcfg.device_info = env_or("WRAPPER_DEVICE_INFO", rcfg.device_info);
            if (!runtime.initialize(loader, rcfg)) {
                std::fprintf(stderr,
                             "wrapper-v2: Apple runtime init failed after dlopen "
                             "succeeded; the loaded libs may be from an unexpected "
                             "APK version. Continuing in stub mode.\n");
            }
        } else {
            std::fprintf(stderr,
                         "wrapper-v2: Apple lib load failed (libs_dir=%s, error=%s). "
                         "Continuing in stub mode; HTTP API works but Apple-backed "
                         "endpoints will return 503.\n",
                         libs_dir.c_str(), loader.last_error().c_str());
        }
    } else {
        std::fprintf(stderr,
                     "wrapper-v2: WRAPPER_APPLE_INIT=0, skipping Apple lib init "
                     "(stub mode: /health and /login work; runtime-backed endpoints do not)\n");
    }

    httplib::Server svr;
    g_server.store(&svr);

    wrapper::Server server(svr, runtime, loader, auth, info);
    server.mount();

    std::fprintf(stderr,
                 "wrapper-v2: %s listening on %s:%d (apple_init=%s, loader_ok=%s, runtime_ready=%s)\n",
                 kVersion, args.host.c_str(), args.port,
                 info.apple_init_enabled ? "on" : "off",
                 loader.ok() ? "yes" : "no",
                 runtime.initialized() ? "yes" : "no");

    if (!svr.listen(args.host, args.port)) {
        std::fprintf(stderr, "wrapper-v2: bind failed on %s:%d\n",
                     args.host.c_str(), args.port);
        return 1;
    }
    return 0;
}

# wrapper-v2

A clean rewrite of the Apple Music FairPlay decryption wrapper. Currently in
**Phase 1** - the daemon initializes Apple's native libraries at startup and
exposes a small HTTP API for managing the stored Media User Token. M3U8 and
decrypt endpoints are coming in subsequent Phase 1 commits.

## What it is

A small daemon that runs Apple Music's Android native libraries inside a
Linux chroot, exposes a local HTTP API for FairPlay key fetching and sample
decryption, and gives downstream tooling (e.g. [`gamdl`](https://github.com/glomatico/gamdl))
a uniform interface that does not depend on platform or language.

The daemon ships *no* Apple code. At build time, libraries are extracted from
a pinned Apple Music for Android APK split (3.6.0-beta, build 1109) whose
SHA-256 digests are committed in `LIBS_VERSION.json`. Without a matching APK
the build fails loudly.

## Status

| Phase | Goal | State |
| --- | --- | --- |
| 0 | Repo skeleton, build chain, NDK toolchain, CI smoke test | **Done** |
| 1.0 | Apple lib runtime init at startup, `/login` + `/me` endpoints | **In progress** |
| 1.1 | `/storefront`, `/dev-token` endpoints (Media User Token unused) | Pending |
| 1.2 | `/m3u8` (asset URL fetch) | Pending |
| 1.3 | `/decrypt` (full MP4 round-trip, batch sample decrypt) | Pending |
| 2 | Caching, rate limit, dedupe, request queue | Pending |
| 3 | arm64-v8a build, multi-arch Docker | Pending |

## HTTP API

Every endpoint accepts and returns `application/json`.

| Method | Path | Description |
| --- | --- | --- |
| `GET` | `/health` | Liveness probe. Returns `{status, phase, version}`. |
| `GET` | `/me` | Snapshot of `{runtime, auth, version}`. Reports whether the Apple runtime initialized cleanly and whether a Media User Token is currently cached. |
| `POST` | `/login` | Body: `{"media_user_token": "..."}`. Caches the token in process memory. Returns the token preview and the timestamp it was set. |
| `DELETE` | `/login` | Drops the cached Media User Token. Idempotent. |

The Media User Token is the credential Apple's modern Music API uses for
per-user requests. You obtain it out-of-band - typically from an iOS
device, an Apple Music web session, or a separate token-extraction tool.
This wrapper never accepts an Apple ID password and does not run sign-in
flows; the token *is* the credential.

## Layout

```
.
├── CMakeLists.txt            top-level build (host launcher + NDK sub-build)
├── Dockerfile                multi-stage build
├── compose.yaml              docker compose entrypoint
├── LIBS_VERSION.json         pinned APK + per-.so SHA-256 digests
├── src/
│   ├── daemon/               C++ daemon (cross-compiled with the NDK)
│   │   ├── CMakeLists.txt
│   │   ├── main.cpp          process entry: env parsing, lifecycle
│   │   ├── server.{hpp,cpp}  HTTP route mounting (cpp-httplib)
│   │   └── apple/
│   │       ├── abi.hpp       Apple-lib mangled symbol declarations
│   │       ├── auth.{hpp,cpp}    Media User Token storage
│   │       └── runtime.{hpp,cpp} FootHillConfig + DeviceGUID + RequestContext init
│   └── launcher/
│       └── wrapper.c         host-Linux chroot launcher
├── rootfs/                   chroot tree assembled at build time
│   └── system/
│       ├── bin/              <- main, linker64 (staged)
│       └── lib64/            <- Apple's .so + Android system .so (staged)
├── tools/
│   ├── fetch-apk.sh          download an APK / .apkm, verify SHA-256
│   ├── extract-libs.sh       extract .so files from APK, verify SHA-256
│   └── stage-system.sh       copy committed Android binaries into rootfs/
└── vendor/
    └── android-system/       linker64 + bionic + AOSP libs, SHA-pinned
        └── x86_64/
            ├── bin/linker64
            └── lib64/{libc, libm, libstdc++, liblog, libz, libandroid, libOpenSLES}.so
```

## Building

### One-time setup

You need a working Docker installation. Apart from that, the entire build
runs inside the image. There is no host toolchain prerequisite for the
default workflow.

For the build to succeed you must obtain Apple Music for Android **3.6.0-beta
(1109)** as APK splits. The APK is *not* committed and *not* fetched
automatically by the Dockerfile.

### Local build

```bash
# 1. Fetch the APKMirror bundle (you provide the URL).
APK_URL=https://your-mirror.example/apple-music-3.6.0-beta-1109.apkm \
    tools/fetch-apk.sh --expect apkm \
                        --out    .tmp/bundle.apkm

# 2. Extract Apple libs into rootfs/, verifying SHA-256 at every step
#    (bundle, inner split, every individual .so).
tools/extract-libs.sh --bundle .tmp/bundle.apkm \
                       --arch   x86_64 \
                       --out    rootfs/system/lib64

# 3. Stage the committed Android system binaries (linker64 + bionic + AOSP)
#    into rootfs/, verifying their SHA-256 against LIBS_VERSION.json.
tools/stage-system.sh --arch x86_64

# 4. Build and run.
docker compose up --build

# 5. Smoke-test.
curl http://127.0.0.1/health
curl http://127.0.0.1/me

# 6. Cache a Media User Token.
curl -X POST http://127.0.0.1/login \
     -H 'content-type: application/json' \
     -d '{"media_user_token": "AyL...your-token..."}'
curl http://127.0.0.1/me        # logged_in: true
curl -X DELETE http://127.0.0.1/login
```

The daemon binds port 80 inside the container and the compose file maps it
to host port 80 by default. Override with `HTTP_PORT=8080 docker compose up`
on machines that already have something on `:80`.

### Daemon configuration

The daemon reads `WRAPPER_*` environment variables (forwarded via
`compose.yaml`). See `.env.example` for the full list. The most useful are:

- `WRAPPER_HOST`, `WRAPPER_PORT` - bind address inside the chroot.
- `WRAPPER_BASE_DIR` - filesystem dir Apple's libs use for the FairPlay
  key cache and `mpl_db`. The default matches upstream wrapper.
- `WRAPPER_DEVICE_INFO` - 9-tuple identifying the fake Apple Music
  Android client. Same fingerprint upstream uses by default.
- `WRAPPER_APPLE_INIT=0` - skip Apple lib initialization at startup.
  Lets you bring up the HTTP server alone for `/health` smoke tests
  even on builds where you have not staged the Apple libraries yet.

If you already have a single `split_config.x86_64.apk` rather than an
`.apkm` bundle, swap step 2 for `tools/extract-libs.sh --apk path/to/split_config.x86_64.apk ...` -
the same SHA verification still applies.

### CI build

The `.github/workflows/build.yml` workflow does the same three steps using
a single repository secret:

- `APK_URL` - URL of the pinned `.apkm` bundle (contains every split)

Pull requests opened from forks skip the build job (they cannot read the
secret).

## License

[Unlicense](./LICENSE) - public domain dedication.

This project is not affiliated with Apple Inc. The Apple-authored libraries
it loads at runtime are not redistributed by this repository.

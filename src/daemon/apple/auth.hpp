// Authenticated session state for wrapper-v2.
//
// Phase 1.0 deliberately avoids upstream's email/password sign-in
// flow entirely. Instead, the user POSTs a Media User Token they
// already obtained out-of-band (e.g., from an iOS device, an Apple
// Music web session, or a tool like apple-music-token-extractor),
// and we cache it in process memory.
//
// The Media User Token is the credential Apple's modern Music API
// uses to authorize per-user requests. It does NOT, by itself, get
// us through FairPlay license requests - those still require an
// iTunes Store auth context. Wiring the token into Apple's URL
// request machinery for downstream calls is a Phase 1.1+ job; this
// module only owns storage.

#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>

namespace wrapper::apple {

class AuthState {
public:
    AuthState() = default;

    AuthState(const AuthState&) = delete;
    AuthState& operator=(const AuthState&) = delete;

    // Replace the stored Media User Token. Empty input clears.
    void set_media_user_token(std::string token);

    // Drop any stored credentials.
    void clear();

    // True iff a non-empty Media User Token is stored.
    bool logged_in() const;

    // Best-effort log-safe preview of the stored token (first 14
    // chars + ellipsis, or empty if not logged in).
    std::string token_preview() const;

    // Returns a copy of the stored token (or std::nullopt). Use
    // sparingly - the returned string contains a secret.
    std::optional<std::string> token() const;

    // Wall-clock time when the current token was set. Returns
    // std::chrono::system_clock::time_point::min() if not logged in.
    std::chrono::system_clock::time_point logged_in_at() const;

private:
    mutable std::mutex mu_;
    std::string token_;
    std::chrono::system_clock::time_point set_at_{};
};

}  // namespace wrapper::apple

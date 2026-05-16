#include "apple/auth.hpp"

namespace wrapper::apple {

void AuthState::set_media_user_token(std::string token) {
    std::lock_guard<std::mutex> g(mu_);
    token_ = std::move(token);
    if (token_.empty()) {
        set_at_ = {};
    } else {
        set_at_ = std::chrono::system_clock::now();
    }
}

void AuthState::clear() {
    std::lock_guard<std::mutex> g(mu_);
    token_.clear();
    set_at_ = {};
}

bool AuthState::logged_in() const {
    std::lock_guard<std::mutex> g(mu_);
    return !token_.empty();
}

std::string AuthState::token_preview() const {
    std::lock_guard<std::mutex> g(mu_);
    if (token_.empty()) return {};
    if (token_.size() <= 14) return token_;
    return token_.substr(0, 14) + "...";
}

std::optional<std::string> AuthState::token() const {
    std::lock_guard<std::mutex> g(mu_);
    if (token_.empty()) return std::nullopt;
    return token_;
}

std::chrono::system_clock::time_point AuthState::logged_in_at() const {
    std::lock_guard<std::mutex> g(mu_);
    return set_at_;
}

}  // namespace wrapper::apple

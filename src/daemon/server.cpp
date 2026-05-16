#include "server.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

#include <nlohmann/json.hpp>

namespace wrapper {

namespace {

using nlohmann::json;

void respond_json(httplib::Response& res, int status, json body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

std::string iso8601_utc(std::chrono::system_clock::time_point tp) {
    if (tp.time_since_epoch().count() == 0) return {};
    auto t = std::chrono::system_clock::to_time_t(tp);
    char buf[32];
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return buf;
}

}  // namespace

Server::Server(httplib::Server& svr,
               apple::Runtime& rt,
               apple::Loader& loader,
               apple::AuthState& auth,
               ServerInfo info)
    : svr_(svr), rt_(rt), loader_(loader), auth_(auth), info_(std::move(info)) {}

void Server::mount() {
    svr_.Get("/health", [this](const httplib::Request&, httplib::Response& res) {
        json body = {
            {"status", "ok"},
            {"phase", 1},
            {"version", info_.version},
        };
        respond_json(res, 200, std::move(body));
    });

    svr_.Get("/me", [this](const httplib::Request&, httplib::Response& res) {
        json runtime = {
            {"apple_init_enabled", info_.apple_init_enabled},
            {"loader_ok",          loader_.ok()},
            {"initialized",        rt_.initialized()},
        };
        if (!loader_.ok() && !loader_.last_error().empty()) {
            runtime["loader_error"] = loader_.last_error();
        }
        if (rt_.initialized()) {
            runtime["base_dir"] = rt_.base_dir();
            runtime["device_info"] = rt_.device_info();
        }

        json auth = {{"logged_in", auth_.logged_in()}};
        if (auth_.logged_in()) {
            auth["media_user_token_preview"] = auth_.token_preview();
            auth["logged_in_at"] = iso8601_utc(auth_.logged_in_at());
        }

        respond_json(res, 200, json{
            {"runtime", std::move(runtime)},
            {"auth", std::move(auth)},
            {"version", info_.version},
        });
    });

    svr_.Post("/login", [this](const httplib::Request& req, httplib::Response& res) {
        json req_body;
        try {
            req_body = json::parse(req.body);
        } catch (const std::exception& e) {
            respond_json(res, 400, json{
                {"error", "invalid_json"},
                {"detail", e.what()},
            });
            return;
        }

        if (!req_body.is_object() || !req_body.contains("media_user_token")
            || !req_body["media_user_token"].is_string()) {
            respond_json(res, 400, json{
                {"error", "missing_field"},
                {"detail", "expected JSON object with string 'media_user_token'"},
            });
            return;
        }

        std::string token = req_body["media_user_token"].get<std::string>();
        if (token.empty()) {
            respond_json(res, 400, json{
                {"error", "empty_token"},
                {"detail", "media_user_token must be non-empty; use DELETE /login to clear"},
            });
            return;
        }

        auth_.set_media_user_token(std::move(token));
        std::fprintf(stderr, "server: stored media_user_token (preview=%s)\n",
                     auth_.token_preview().c_str());

        respond_json(res, 200, json{
            {"logged_in", true},
            {"media_user_token_preview", auth_.token_preview()},
            {"logged_in_at", iso8601_utc(auth_.logged_in_at())},
        });
    });

    svr_.Delete("/login", [this](const httplib::Request&, httplib::Response& res) {
        bool was_logged_in = auth_.logged_in();
        auth_.clear();
        if (was_logged_in) {
            std::fprintf(stderr, "server: cleared media_user_token\n");
        }
        respond_json(res, 200, json{
            {"logged_in", false},
            {"cleared", was_logged_in},
        });
    });

    svr_.set_exception_handler([](const httplib::Request&, httplib::Response& res,
                                  std::exception_ptr ep) {
        std::string what = "unknown";
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
        }
        respond_json(res, 500, json{{"error", "internal"}, {"detail", what}});
    });
}

}  // namespace wrapper

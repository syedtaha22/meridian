// HTTP server for the chess engine.
//
// Endpoints:
//   GET  /health   engine identity and limits
//   POST /move     {fen, moves?, depth?, time_limit_ms?} -> best move and score
//
// Environment: PORT (8080), CHESS_MAX_DEPTH (12), CHESS_DEFAULT_DEPTH (6),
// CHESS_MAX_TIME_MS (10000), CHESS_HASH_MB (64), CHESS_THREADS (4).

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include <httplib.h>

#include "api.hpp"

namespace {

    httplib::Server *g_server = nullptr;

    void on_signal(int) {
        if (g_server != nullptr) {
            g_server->stop();
        }
    }

    long env_number(const char *name, long fallback) {
        const char *value = std::getenv(name);

        if (value == nullptr || *value == '\0') {
            return fallback;
        }

        char *end = nullptr;
        const long parsed = std::strtol(value, &end, 10);
        return (end != value && *end == '\0' && parsed > 0) ? parsed : fallback;
    }

    std::string env_text(const char *name, const std::string &fallback) {
        const char *value = std::getenv(name);

        return (value == nullptr || *value == '\0') ? fallback : value;
    }

    void add_cors(httplib::Response &res, const std::string &origin) {
        res.set_header("Access-Control-Allow-Origin", origin);
        res.set_header("Vary", "Origin");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    }

}  // namespace

int main() {
    chess::api::Config config;
    config.max_depth = static_cast<int>(env_number("CHESS_MAX_DEPTH", config.max_depth));
    config.default_depth =
        static_cast<int>(env_number("CHESS_DEFAULT_DEPTH", config.default_depth));
    config.max_time_ms = env_number("CHESS_MAX_TIME_MS", config.max_time_ms);
    config.hash_mb =
        static_cast<size_t>(env_number("CHESS_HASH_MB", static_cast<long>(config.hash_mb)));
    const int port = static_cast<int>(env_number("PORT", 8080));
    const size_t threads = static_cast<size_t>(env_number("CHESS_THREADS", 4));
    const std::string origin = env_text("CHESS_ALLOWED_ORIGIN", "https://syedtaha22.github.io");

    const chess::api::Service service(config);
    httplib::Server server;
    server.new_task_queue = [threads] { return new httplib::ThreadPool(threads); };
    server.set_payload_max_length(64 * 1024);

    auto reply = [&origin](httplib::Response &res, const chess::api::Response &r) {
        add_cors(res, origin);
        res.status = r.status;
        res.set_content(r.body, "application/json");
    };

    server.Get("/health", [&](const httplib::Request &, httplib::Response &res) {
        reply(res, service.health());
    });
    server.Post("/move", [&](const httplib::Request &req, httplib::Response &res) {
        reply(res, service.move(req.body));
    });
    server.Options(".*", [&origin](const httplib::Request &, httplib::Response &res) {
        add_cors(res, origin);
        res.status = 204;
    });
    server.set_error_handler([&origin](const httplib::Request &, httplib::Response &res) {
        add_cors(res, origin);

        if (res.body.empty()) {
            res.set_content("{\"error\": \"not found\"}", "application/json");
        }
    });

    g_server = &server;
    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);

    std::cout << "chess engine " << config.version << " listening on port " << port
              << " (max depth " << config.max_depth << ", " << config.hash_mb << " MB hash, "
              << threads << " workers)" << std::endl;
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "could not listen on port " << port << std::endl;
        return 1;
    }
    return 0;
}

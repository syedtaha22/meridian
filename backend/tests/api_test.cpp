// Service tests: request validation, reply shape and an end to end HTTP round trip.
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "../server/api.hpp"

namespace {

    using json = nlohmann::json;
    int failures = 0;

    void check(bool condition, const std::string &what) {
        if (!condition) {
            ++failures;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    const char *kStart = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    void test_health(const chess::api::Service &service) {
        const auto reply = service.health();
        const json body = json::parse(reply.body);
        check(reply.status == 200, "health is 200");
        check(body["status"] == "ok", "health status ok");
        check(body.contains("engine_version") && body.contains("architecture") &&
                  body.contains("max_depth") && body.contains("hash_mb"),
              "health has the documented fields");
    }

    void test_move(const chess::api::Service &service) {
        json request = {{"fen", kStart}, {"moves", {"e2e4", "e7e5"}}, {"depth", 4}};
        auto reply = service.move(request.dump());
        check(reply.status == 200, "move is 200");
        json body = json::parse(reply.body);
        check(body["move"].is_string() && body["move"].get<std::string>().size() >= 4,
              "move is a UCI string");
        check(body["score"].contains("cp") && body["score"].contains("mate"),
              "score has cp and mate");
        check(body["depth"].get<int>() >= 1, "depth reported");
        check(body["nodes"].get<int64_t>() > 0, "nodes reported");
        check(body["elapsed_ms"].get<double>() >= 0, "elapsed reported");

        // A forced mate is reported as a mate, not as centipawns.
        request = {{"fen", "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1"}, {"depth", 4}};
        body = json::parse(service.move(request.dump()).body);
        check(body["move"] == "a1a8" && body["score"]["mate"] == 1 && body["score"]["cp"].is_null(),
              "mate in one is reported as mate 1");
    }

    void test_validation(const chess::api::Service &service) {
        check(service.move("not json").status == 400, "malformed JSON is 400");
        check(service.move("{}").status == 422, "missing fen is 422");
        check(service.move(R"({"fen": "nonsense"})").status == 422, "invalid fen is 422");
        check(service.move(json{{"fen", kStart}, {"moves", {"e2e5"}}}.dump()).status == 422,
              "illegal move in the list is 422");
        check(service.move(json{{"fen", kStart}, {"depth", 0}}.dump()).status == 422,
              "depth below range is 422");
        check(service.move(json{{"fen", kStart}, {"depth", 999}}.dump()).status == 422,
              "depth above range is 422");
        check(service.move(json{{"fen", kStart}, {"depth", "deep"}}.dump()).status == 422,
              "non-integer depth is 422");
        check(service.move(json{{"fen", kStart}, {"time_limit_ms", -1}}.dump()).status == 422,
              "negative time limit is 422");
        check(service.move(json{{"fen", "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"}}.dump()).status == 422,
              "position without legal moves is 422");
    }

    void test_time_limit(const chess::api::Service &service) {
        const auto start = std::chrono::steady_clock::now();
        const auto reply = service.move(json{
            {"fen", "r1bq1rk1/ppp2ppp/2n2n2/3pp3/1bP5/2NP1NP1/PP2PPBP/R1BQ1RK1 w - - 0 1"},
            {"depth", 12},
            {"time_limit_ms", 100}}.dump());
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        check(reply.status == 200 && ms < 500,
              "time limit is respected (" + std::to_string(ms) + " ms)");
    }

    void test_http(const chess::api::Service &service) {
        httplib::Server server;

        server.Get("/health", [&](const httplib::Request &, httplib::Response &res) {
            res.set_content(service.health().body, "application/json");
        });

        server.Post("/move", [&](const httplib::Request &req, httplib::Response &res) {
            const auto r = service.move(req.body);
            res.status = r.status;
            res.set_content(r.body, "application/json");
        });

        const int port = server.bind_to_any_port("127.0.0.1");
        std::thread thread([&server] { server.listen_after_bind(); });
        server.wait_until_ready();

        httplib::Client client("127.0.0.1", port);
        const auto health = client.Get("/health");
        check(health && health->status == 200, "HTTP GET /health");
        const auto move =
            client.Post("/move", json{{"fen", kStart}, {"depth", 2}}.dump(), "application/json");
        check(move && move->status == 200 && json::parse(move->body).contains("move"),
              "HTTP POST /move");
        const auto bad = client.Post("/move", "{}", "application/json");
        check(bad && bad->status == 422, "HTTP error status passes through");

        server.stop();
        thread.join();
    }

}  // namespace

int main() {
    chess::api::Config config;
    config.hash_mb = 8;

    const chess::api::Service service(config);

    test_health(service);
    test_move(service);
    test_validation(service);
    test_time_limit(service);
    test_http(service);

    if (failures == 0) {
        std::printf("all api tests passed\n");
        return 0;
    }

    std::printf("%d failures\n", failures);
    return 1;
}

#include "api.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <random>
#include <vector>

#include <nlohmann/json.hpp>

#include "chess/book.hpp"
#include "chess/movegen.hpp"
#include "chess/position.hpp"
#include "chess/search.hpp"

namespace chess::api {

    namespace {

        using json = nlohmann::json;

        Response error(int status, const std::string &message) {
            return {status, json{{"error", message}}.dump()};
        }

        json score_json(int score) {
            const Score display = to_display_score(score);

            if (display.is_mate) {
                return {{"cp", nullptr}, {"mate", display.value}};
            }
            return {{"cp", display.value}, {"mate", nullptr}};
        }

        /**
         * A request after validation: the position, the hashes before it, and the limits.
         */
        struct Request {
            Position position;
            std::vector<uint64_t> history;
            int depth = 0;
            int64_t time_limit_ms = 0;
            bool use_book = false;
        };

        /**
         * Parses and validates a body; on failure fills `failure` and returns false.
         */
        bool parse_request(const std::string &body, const Config &config, Request &request,
                           Response &failure) {
            json doc;
            try {
                doc = json::parse(body);
            } catch (const json::exception &) {
                failure = error(400, "request body is not valid JSON");
                return false;
            }
            if (!doc.is_object() || !doc.contains("fen") || !doc["fen"].is_string()) {
                failure = error(422, "\"fen\" must be a string");
                return false;
            }

            auto position = Position::from_fen(doc["fen"].get<std::string>());

            if (!position) {
                failure = error(422, "\"fen\" is not a valid position");
                return false;
            }

            request.position = *position;
            request.history.clear();

            if (doc.contains("moves")) {
                if (!doc["moves"].is_array()) {
                    failure = error(422, "\"moves\" must be an array of UCI moves");
                    return false;
                }

                for (const json &entry : doc["moves"]) {
                    if (!entry.is_string()) {
                        failure = error(422, "\"moves\" must be an array of UCI moves");
                        return false;
                    }

                    const Move move = move_from_uci(request.position, entry.get<std::string>());

                    if (move.is_null()) {
                        failure = error(422, "illegal move in \"moves\": " + entry.dump());
                        return false;
                    }

                    request.history.push_back(request.position.key());
                    request.position.make_move(move);
                }
            }

            request.depth = config.default_depth;

            if (doc.contains("depth")) {
                if (!doc["depth"].is_number_integer()) {
                    failure = error(422, "\"depth\" must be an integer");
                    return false;
                }

                request.depth = doc["depth"].get<int>();

                if (request.depth < 1 || request.depth > config.max_depth) {
                    failure = error(422, "\"depth\" must be between 1 and " +
                                             std::to_string(config.max_depth));
                    return false;
                }
            }

            request.use_book = config.use_book;

            if (doc.contains("book")) {
                if (!doc["book"].is_boolean()) {
                    failure = error(422, "\"book\" must be a boolean");
                    return false;
                }
                request.use_book = doc["book"].get<bool>();
            }

            request.time_limit_ms = config.max_time_ms;

            if (doc.contains("time_limit_ms")) {
                if (!doc["time_limit_ms"].is_number_integer()) {
                    failure = error(422, "\"time_limit_ms\" must be an integer");
                    return false;
                }

                const int64_t asked = doc["time_limit_ms"].get<int64_t>();

                if (asked < 0 || asked > config.max_time_ms) {
                    failure = error(422, "\"time_limit_ms\" must be between 0 and " +
                                             std::to_string(config.max_time_ms));
                    return false;
                }

                // Zero means no limit from the client, which the server still caps.
                if (asked > 0) {
                    request.time_limit_ms = asked;
                }
            }

            MoveList legal;
            generate_legal(request.position, legal);

            if (legal.empty()) {
                failure = error(422, "the position has no legal moves");
                return false;
            }
            return true;
        }

        /**
         * One searcher per worker thread, so concurrent requests do not share state.
         */
        Searcher &thread_searcher(const Config &config) {
            thread_local std::unique_ptr<Searcher> searcher;

            if (!searcher) {
                SearchOptions options;
                options.hash_mb = config.hash_mb;
                searcher = std::make_unique<Searcher>(options);
            }
            return *searcher;
        }

        /**
         * A random number for the choice among book moves, from a generator of the thread.
         */
        uint64_t book_random() {
            thread_local std::mt19937_64 generator{std::random_device{}()};
            return generator();
        }

    }  // namespace

    Service::Service(Config config) : config_(std::move(config)) {
    }

    Response Service::health() const {
        json reply = {
            {"status", "ok"},
            {"engine_version", config_.version},
            {"architecture", "alpha-beta, PVS, SEE, futility, null move, LMR, hash table, tuned "
                             "evaluation, opening book"},
            {"max_depth", config_.max_depth},
            {"hash_mb", config_.hash_mb},
        };

        return {200, reply.dump()};
    }

    Response Service::move(const std::string &body) const {
        Request request;
        Response failure;

        if (!parse_request(body, config_, request, failure)) {
            return failure;
        }

        if (request.use_book) {
            const auto choice = Book::builtin().pick(request.position, book_random());

            if (choice) {
                json reply = {
                    {"move", move_to_uci(choice->move)},
                    {"score", score_json(choice->score)},
                    {"depth", choice->depth},
                    {"nodes", 0},
                    {"elapsed_ms", 0.0},
                    {"book", true},
                };

                return {200, reply.dump()};
            }
        }

        Searcher &searcher = thread_searcher(config_);
        searcher.options().max_depth = request.depth;
        searcher.options().time_limit_ms = request.time_limit_ms;

        const SearchResult result = searcher.search(request.position, request.history);
        json reply = {
            {"move", move_to_uci(result.best_move)},
            {"score", score_json(result.score)},
            {"depth", result.depth},
            {"nodes", result.stats.nodes},
            {"elapsed_ms", result.elapsed_ms},
            {"book", false},
        };

        return {200, reply.dump()};
    }

}  // namespace chess::api

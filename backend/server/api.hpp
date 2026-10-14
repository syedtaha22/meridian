#pragma once

#include <cstddef>
#include <string>

namespace chess::api {

    /**
     * Limits and defaults of the service.
     */
    struct Config {
        int max_depth = 12;           ///< Largest search depth a request may ask for.
        int default_depth = 6;        ///< Depth used when a request names none.
        int64_t max_time_ms = 10000;  ///< Longest search a request may ask for.
        size_t hash_mb = 64;          ///< Transposition table size per worker thread.
        bool use_book = true;         ///< Play opening book moves when the position is in the book.
        std::string version = "0.1.0";
    };

    /**
     * The status code and JSON body of a reply.
     */
    struct Response {
        int status = 200;
        std::string body;
    };

    /**
     * @brief The service's endpoints, independent of the HTTP library serving them.
     *
     * Each call parses and validates a JSON request body, searches, and returns the
     * JSON reply. Requests are independent; each thread keeps its own searcher and
     * transposition table, which later requests on that thread reuse.
     */
    class Service {
    public:
        /**
         * @brief Creates the service.
         *
         * @param config Limits and defaults.
         */
        explicit Service(Config config = {});

        /**
         * @brief Reports the engine's identity and limits.
         *
         * @return 200 with `{status, engine_version, architecture, max_depth, hash_mb}`.
         */
        Response health() const;

        /**
         * @brief Chooses a move.
         *
         * Request: `{fen, moves?, depth?, time_limit_ms?, book?}`, where `moves` are UCI moves
         * played since `fen`, and `book` turns the opening book on or off for the request. Reply:
         * `{move, score: {cp, mate}, depth, nodes, elapsed_ms, book}`, where `book` is true when
         * the move came from the opening book, which has no search of its own (`nodes` is 0).
         *
         * @param body JSON request body.
         * @return 200 with the reply, 400 for malformed JSON, or 422 for an invalid or
         *         terminal position or out-of-range parameter.
         */
        Response move(const std::string &body) const;

        /**
         * The configuration.
         */
        const Config &config() const noexcept {
            return config_;
        }

    private:
        Config config_;
    };

}  // namespace chess::api

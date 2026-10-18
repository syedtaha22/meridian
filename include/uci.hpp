/*
  Meridian, a UCI chess engine
  Copyright (C) 2026 Syed Taha

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <atomic>
#include <condition_variable>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "position.hpp"
#include "search.hpp"

namespace chess::uci {

    /**
     * The program name, the version and the author on one line.
     */
    std::string banner();

    /**
     * The text of the `help` command: what the program is, how to start it and its options.
     */
    std::string help();

    /**
     * @brief The engine as a UCI program.
     *
     * Commands come from the input stream, one to a line, and replies go to the output stream.
     * A `go` runs on a thread of its own, so that `stop` and `isready` are answered while the
     * search runs. The searcher and its transposition table persist across moves of a game.
     */
    class Engine {
    public:
        /**
         * @param in Source of commands.
         * @param out Destination of replies; it is flushed after every line.
         */
        Engine(std::istream &in, std::ostream &out);

        ~Engine();

        Engine(const Engine &) = delete;
        Engine &operator=(const Engine &) = delete;

        /**
         * @brief Reads and runs commands until `quit` or the end of the input.
         *
         * At the end of the input, a search with limits runs to its end, and an infinite search
         * is stopped.
         *
         * @return 0.
         */
        int run();

        /**
         * @brief Searches the built-in positions and prints the node count and the speed.
         *
         * @param depth Depth of every search.
         * @return The total nodes searched, which does not change between runs.
         */
        uint64_t bench(int depth);

    private:
        struct Settings {
            int hash_mb = 64;
            int threads = 1;
            int move_overhead_ms = 30;
            bool own_book = false;
            std::string config =
                "network";  ///< A preset with changes, see `SearchOptions::preset`.
            std::shared_ptr<const nnue::Network> network;  ///< From `EvalFile`, or null.
        };

        struct GoCommand {
            int64_t wtime = 0;
            int64_t btime = 0;
            int64_t winc = 0;
            int64_t binc = 0;
            int moves_to_go = 0;
            int64_t move_time = 0;
            int depth = 0;
            uint64_t nodes = 0;
            bool infinite = false;
            bool ponder = false;
            std::vector<Move> search_moves;
        };

        bool handle(const std::string &line);
        void print_identity();
        void set_option(const std::vector<std::string> &tokens);
        void set_position(const std::vector<std::string> &tokens);
        void go(const std::vector<std::string> &tokens);
        void perft(int depth);
        void ponder_hit();
        void start_search(const GoCommand &command);
        void finish_search(bool stop_it);
        void new_game();
        void send(const std::string &line);
        void make_searcher();

        std::istream &in_;
        std::ostream &out_;
        std::mutex out_lock_;

        Settings settings_;
        Position position_;
        std::vector<uint64_t> history_;
        std::unique_ptr<Searcher> searcher_;

        std::thread worker_;
        std::atomic<bool> searching_{false};
        std::atomic<bool> stop_requested_{false};
        bool infinite_ = false;
        GoCommand pondered_;  ///< The command of a `go ponder`, applied by `ponderhit`.
        std::atomic<bool> pondering_{false};
        std::atomic<bool> quiet_{false};  ///< The search that ends sends no `bestmove`.
        std::mutex wait_lock_;
        std::condition_variable wait_;
    };

}  // namespace chess::uci

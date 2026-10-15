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

// Plays the engine against itself and writes labelled positions for tuning the evaluation.
//
// Usage: gen_positions [--games 20000] [--depth 7] [--config full] [--threads 24]
//                      [--random-plies 8] [--skip 3] [--seed 1] [--nnue FILE]
//                      [--out positions.txt]
//
// Every game starts from random legal moves. After the opening, every `--skip` plies a quiet
// position is kept: the side to move is not in check, the search found no capture or promotion
// as the best move, and the score is not a mate. A game ends at a mate, a draw by the rules, or
// when one side is far ahead for several moves in a row. Each line is
// `FEN<TAB>result<TAB>game<TAB>score`, where the result is 1, 0.5 or 0 for White, and the score is
// the search score in centipawns from the side of White.

#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>

#include "../bench/common.hpp"
#include "movegen.hpp"
#include "nnue.hpp"
#include "search.hpp"

namespace {

    using namespace chess;

    /**
     * Plays one game; returns the result for White and the positions it kept.
     */
    double play(uint64_t seed, const std::string &config, int depth, int random_plies, int skip,
                const std::shared_ptr<const nnue::Network> &network,
                std::vector<std::string> &kept) {
        std::mt19937_64 random(seed);
        Position pos;
        std::vector<uint64_t> history;

        for (int ply = 0; ply < random_plies; ++ply) {
            MoveList legal;
            generate_legal(pos, legal);

            if (legal.empty()) {
                return 0.5;
            }

            history.push_back(pos.key());
            pos.make_move(legal[static_cast<int>(random() % static_cast<uint64_t>(legal.size()))]);
        }

        SearchOptions options = SearchOptions::preset(config, depth);
        options.hash_mb = 16;
        if (network) {
            options.nnue = network;
        }
        Searcher searcher(options);
        int ahead_white = 0;
        int ahead_black = 0;

        for (int ply = 0; ply < 400; ++ply) {
            MoveList legal;
            generate_legal(pos, legal);

            if (legal.empty()) {
                if (!pos.in_check()) {
                    return 0.5;
                }
                return pos.side_to_move() == kWhite ? 0.0 : 1.0;
            }

            if (pos.halfmove_clock() >= 100 || pos.insufficient_material()) {
                return 0.5;
            }

            int repeats = 0;

            for (uint64_t key : history) {
                repeats += key == pos.key();
            }
            if (repeats >= 2) {
                return 0.5;
            }

            const SearchResult result = searcher.search(pos, history);
            const int white_score = pos.side_to_move() == kWhite ? result.score : -result.score;

            ahead_white = white_score > 1000 ? ahead_white + 1 : 0;
            ahead_black = white_score < -1000 ? ahead_black + 1 : 0;

            if (ahead_white >= 4) {
                return 1.0;
            }
            if (ahead_black >= 4) {
                return 0.0;
            }

            const bool quiet = !pos.in_check() && !result.best_move.is_null() &&
                               !result.best_move.is_capture() && !result.best_move.is_promotion() &&
                               std::abs(result.score) < kMateBound;

            if (quiet && ply % skip == 0) {
                kept.push_back(pos.to_fen() + '\t' + std::to_string(white_score));
            }

            history.push_back(pos.key());
            pos.make_move(result.best_move);
        }
        return 0.5;
    }

}  // namespace

int main(int argc, char **argv) {
    const bench::Args args(argc, argv);
    const int games = args.get_int("games", 20000);
    const int depth = args.get_int("depth", 7);
    const std::string config = args.get("config", "full");
    const int threads = std::max(1, args.get_int("threads", 24));
    const int random_plies = args.get_int("random-plies", 8);
    const int skip = std::max(1, args.get_int("skip", 3));
    const uint64_t seed = static_cast<uint64_t>(args.get_int("seed", 1));
    const std::string path = args.get("out", "positions.txt");
    std::shared_ptr<const nnue::Network> network;

    if (args.has("nnue")) {
        network = nnue::load(args.get("nnue", ""));

        if (!network) {
            std::cerr << "cannot read the network\n";
            return 1;
        }
    }

    std::ofstream out(path);
    std::mutex lock;
    std::atomic<int> next{0};
    std::atomic<size_t> written{0};
    std::vector<std::thread> pool;

    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            for (int game = next++; game < games; game = next++) {
                std::vector<std::string> kept;
                const double result = play(seed * 1000003ULL + static_cast<uint64_t>(game), config,
                                           depth, random_plies, skip, network, kept);
                std::lock_guard<std::mutex> guard(lock);

                for (const std::string &fen : kept) {
                    // The FEN and the score are one field of the kept string, tab separated.
                    const size_t tab = fen.rfind('\t');
                    out << fen.substr(0, tab) << '\t' << result << '\t' << game << '\t'
                        << fen.substr(tab + 1) << '\n';
                }
                written += kept.size();

                if ((game + 1) % 500 == 0) {
                    std::printf("%d games, %zu positions\n", game + 1, written.load());
                    std::fflush(stdout);
                }
            }
        });
    }

    for (std::thread &thread : pool) {
        thread.join();
    }

    std::printf("%d games, %zu positions written to %s\n", games, written.load(), path.c_str());
    return 0;
}

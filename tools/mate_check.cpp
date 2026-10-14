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

// Solves the mate puzzles with one search configuration and counts the misses.
//
// Usage: mate_check [--config full] [--data tests/data] [--hash 8] [--eval FILE] [--nnue FILE]
//                   [--extra-plies 0] [--verbose]
//
// Every puzzle has a forced mate in 1, 2 or 3 moves. A configuration solves a puzzle when the
// search at depth 2 * moves (plus `--extra-plies`) returns a mate in that many moves or fewer,
// with a legal move.

#include <fstream>
#include <iostream>
#include <memory>

#include "../bench/common.hpp"
#include "movegen.hpp"
#include "search.hpp"

int main(int argc, char **argv) {
    using namespace chess;

    const bench::Args args(argc, argv);
    const std::string config = args.get("config", "full");
    const std::string data = args.get("data", "tests/data");
    const size_t hash_mb = static_cast<size_t>(args.get_int("hash", 8));
    const int extra_plies = args.get_int("extra-plies", 0);

    std::shared_ptr<const EvalParams> eval;
    std::shared_ptr<const nnue::Network> network;

    if (args.has("nnue")) {
        network = nnue::load(args.get("nnue", ""));

        if (!network) {
            std::cerr << "cannot read the network\n";
            return 1;
        }
    }

    if (args.has("eval")) {
        auto params = std::make_shared<EvalParams>(EvalParams::defaults());

        if (!params->load(args.get("eval", ""))) {
            std::cerr << "cannot read evaluation numbers\n";
            return 1;
        }
        eval = params;
    }

    int missed_total = 0;
    uint64_t nodes_total = 0;

    for (int moves = 1; moves <= 3; ++moves) {
        std::ifstream in(data + "/mate_in_" + std::to_string(moves) + ".fen");
        std::string fen;
        int puzzles = 0;
        int missed = 0;

        SearchOptions options = SearchOptions::preset(config, 2 * moves + extra_plies);
        options.hash_mb = hash_mb;
        options.eval_params = eval;
        options.nnue = network;
        Searcher searcher(options);

        while (std::getline(in, fen)) {
            auto pos = Position::from_fen(fen);

            if (!pos) {
                continue;
            }

            ++puzzles;
            searcher.clear();

            const SearchResult result = searcher.search(*pos);
            const Score score = result.display_score();
            nodes_total += result.stats.nodes;

            MoveList legal;
            generate_legal(*pos, legal);
            bool is_legal = false;

            for (const Move move : legal) {
                is_legal = is_legal || move == result.best_move;
            }

            if (!(score.is_mate && score.value > 0 && score.value <= moves && is_legal)) {
                ++missed;

                if (args.has("verbose")) {
                    std::printf("missed mate in %d: %s (score %d)\n", moves, fen.c_str(),
                                result.score);
                }
            }
        }

        std::printf("mate in %d: %d of %d solved\n", moves, puzzles - missed, puzzles);
        missed_total += missed;
    }

    std::printf("%s: %d missed, %llu nodes\n", config.c_str(), missed_total,
                static_cast<unsigned long long>(nodes_total));
    return missed_total == 0 ? 0 : 1;
}

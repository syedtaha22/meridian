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

// Tablebase tests with the small Syzygy tables of tests/data/syzygy: probing, the root move,
// and the use of the tables by the search.
#include <cstdio>
#include <string>

#include "movegen.hpp"
#include "search.hpp"
#include "syzygy.hpp"

namespace {

    int failures = 0;

    void check(bool condition, const std::string &what) {
        if (!condition) {
            ++failures;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    chess::Position parse(const std::string &fen) {
        const auto position = chess::Position::from_fen(fen);

        if (!position) {
            std::printf("FAIL: could not parse %s\n", fen.c_str());
            std::exit(1);
        }
        return *position;
    }

    void test_probing() {
        using chess::syzygy::Wdl;

        const chess::Position queen = parse("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
        check(chess::syzygy::largest() >= 4, "tables of 4 pieces are loaded");

        auto wdl = chess::syzygy::probe_wdl(queen);
        check(wdl.has_value() && *wdl == Wdl::kWin, "queen against king is a win");

        const chess::Position other = parse("4k3/8/8/8/8/8/8/3QK3 b - - 0 1");
        wdl = chess::syzygy::probe_wdl(other);
        check(wdl.has_value() && *wdl == Wdl::kLoss, "queen against king is a loss for the king");

        // A position that is not right after a capture or a pawn move, one with castling rights
        // and one with too many pieces cannot be probed for the result.
        check(!chess::syzygy::probe_wdl(parse("4k3/8/8/8/8/8/8/3QK3 w - - 5 40")).has_value(),
              "a half move clock above 0 is not probed");
        check(!chess::syzygy::probe_wdl(chess::Position()).has_value(),
              "the initial position has too many pieces");

        const auto root = chess::syzygy::probe_root(parse("4k3/8/8/8/8/8/8/3QK3 w - - 7 40"));
        check(root.has_value() && root->wdl == Wdl::kWin && root->dtz > 0 && !root->best.is_null(),
              "the root move of a won position has a distance");

        const auto four = chess::syzygy::probe_wdl(parse("4k3/8/8/8/8/8/3r4/3QK3 w - - 0 1"));
        check(four.has_value(), "a position of 4 pieces is probed");
    }

    void test_conversion() {
        // The tables win the queen ending from the first move: play it out and mate.
        chess::Position pos = parse("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
        bool mated = false;

        for (int ply = 0; ply < 60 && !mated; ++ply) {
            const auto root = chess::syzygy::probe_root(pos);
            chess::MoveList legal;
            chess::generate_legal(pos, legal);

            if (legal.empty()) {
                mated = pos.in_check();
                break;
            }

            check(root.has_value(), "every position of the ending has a root move");

            if (!root) {
                break;
            }
            pos.make_move(root->best);
        }

        check(mated, "playing the table's moves for both sides ends in mate");
    }

    void test_search() {
        chess::SearchOptions options = chess::SearchOptions::preset("tuned", 6);
        options.hash_mb = 8;
        chess::Searcher searcher(options);

        const chess::Position queen = parse("4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
        chess::SearchResult result = searcher.search(queen);
        check(result.stats.tb_hits > 0, "the search used the tables");
        check(result.score >= chess::syzygy::kTbWinScore - 10, "a won position scores as a win");

        // A position with more pieces reaches the tables after captures.
        const chess::Position rook = parse("4k3/8/8/8/8/8/3r4/3QK3 w - - 0 1");
        result = searcher.search(rook);
        check(result.stats.tb_hits > 0, "the search reaches the tables from 4 pieces");
        check(result.score > 100, "queen against rook is better for the queen");

        // With the switch off, the same search ignores the tables.
        searcher.options().tablebases = false;
        searcher.clear();
        result = searcher.search(queen);
        check(result.stats.tb_hits == 0 && result.score < chess::syzygy::kTbWinScore - 1000,
              "no tables are used when the switch is off");
    }

}  // namespace

int main(int argc, char **argv) {
    const std::string path = argc > 1 ? argv[1] : "tests/data/syzygy";

    // Before the tables are loaded nothing can be probed.
    check(chess::syzygy::largest() == 0, "no tables before init");
    check(!chess::syzygy::probe_wdl(parse("4k3/8/8/8/8/8/8/3QK3 w - - 0 1")).has_value(),
          "no probe without tables");
    check(chess::syzygy::init(path), "the tables of the test data are found");

    test_probing();
    test_conversion();
    test_search();

    if (failures == 0) {
        std::printf("all syzygy tests passed\n");
        return 0;
    }

    std::printf("%d failures\n", failures);
    return 1;
}

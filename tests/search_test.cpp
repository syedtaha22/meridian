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

// Search tests: terminal positions, agreement between configurations, limits, repetitions.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "eval.hpp"
#include "movegen.hpp"
#include "search.hpp"

namespace {

    int failures = 0;

    void check(bool condition, const std::string &what) {
        if (!condition) {
            ++failures;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    chess::Position parse(const std::string &fen) {
        auto pos = chess::Position::from_fen(fen);

        if (!pos) {
            std::printf("FAIL: could not parse %s\n", fen.c_str());
            std::exit(1);
        }
        return *pos;
    }

    std::vector<std::string> read_lines(const std::string &path) {
        std::ifstream in(path);
        std::vector<std::string> lines;
        std::string line;

        while (std::getline(in, line)) {
            if (!line.empty() && line[0] != '#') {
                lines.push_back(line);
            }
        }
        return lines;
    }

    bool is_legal(const chess::Position &pos, chess::Move move) {
        chess::MoveList list;
        chess::generate_legal(pos, list);
        return std::find(list.begin(), list.end(), move) != list.end();
    }

    void test_terminal_positions() {
        using namespace chess;
        Searcher searcher(SearchOptions::preset("tuned", 4));

        const SearchResult mated = searcher.search(parse("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
        check(mated.best_move.is_null() && mated.score == 0, "stalemate scores 0 with no move");

        const SearchResult checkmated = searcher.search(parse("R5k1/5ppp/8/8/8/8/8/6K1 b - - 0 1"));
        check(checkmated.best_move.is_null() && checkmated.score == -kMateScore,
              "a checkmated side has no move and the mated score");

        const SearchResult draw = searcher.search(parse("4k3/8/8/8/8/8/8/4K3 w - - 0 1"));
        check(draw.score == 0, "bare kings are a draw");
    }

    void test_results_are_legal() {
        using namespace chess;
        const auto lines = read_lines("tests/data/positions/suite.txt");
        check(!lines.empty(), "suite file is not empty");

        for (const char *name :
             {"baseline", "ordering", "tt", "quiescence", "iterative", "pruning", "full", "tuned",
              "network", "full+delta_pruning+razoring+late_move_pruning",
              "full+lmr_table+iir+singular", "full+tt_buckets+threads=2",
              "full+root_entropy+transposition_history", "full+window_variance+inertness+policy",
              "full+history_malus+history_gravity+history_limit=16384+history_bonus=8",
              "full+history_piece+capture_history+continuation2+counter_first",
              "full+killer_count=1+mvv_scale=64+bad_capture_margin=50+underpromotions_late"}) {
            Searcher searcher(SearchOptions::preset(name, name[0] == 'b' ? 3 : 6));

            for (const std::string &line : lines) {
                const size_t tab = line.rfind('\t');
                const Position pos = parse(line.substr(tab + 1));
                const SearchResult result = searcher.search(pos);
                check(is_legal(pos, result.best_move),
                      std::string(name) + " returns a legal move for " + line.substr(tab + 1));
                check(!result.pv.empty() && result.pv.front() == result.best_move,
                      std::string(name) + " pv starts with the best move");
                // The line must be playable.
                Position walk = pos;
                bool playable = true;

                for (const Move move : result.pv) {
                    playable &= is_legal(walk, move) && walk.make_move(move);
                }

                check(playable, std::string(name) + " pv is a legal line");
            }
        }
    }

    /**
     * Techniques that only change the order or the caching of a fixed-depth search should
     * not change its value.
     */
    void test_value_preserving_techniques() {
        using namespace chess;
        const auto lines = read_lines("tests/data/positions/suite.txt");
        int mismatches = 0;

        for (const std::string &line : lines) {
            const Position pos = parse(line.substr(line.rfind('\t') + 1));
            Searcher plain(SearchOptions::preset("baseline", 3));
            Searcher ordered(SearchOptions::preset("ordering", 3));
            const int a = plain.search(pos).score;
            const int b = ordered.search(pos).score;

            if (a != b) {
                ++mismatches;
                std::printf("  ordering changed the value: %d vs %d at %s\n", a, b, line.c_str());
            }
        }

        check(mismatches == 0, "move ordering does not change the value of a fixed-depth search");

        mismatches = 0;

        for (const std::string &line : lines) {
            const Position pos = parse(line.substr(line.rfind('\t') + 1));
            Searcher ordered(SearchOptions::preset("ordering", 4));
            Searcher cached(SearchOptions::preset("tt", 4));
            const int a = ordered.search(pos).score;
            const int b = cached.search(pos).score;

            if (a != b) {
                ++mismatches;
                std::printf("  tt changed the value: %d vs %d at %s\n", a, b, line.c_str());
            }
        }

        check(mismatches == 0, "the transposition table does not change a fixed-depth value");

        // The full search can differ in value (it prunes) but should agree on mate and material
        // swings.
        const Position tactic =
            parse("r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 0 1");
        Searcher full(SearchOptions::preset("tuned", 5));
        const SearchResult result = full.search(tactic);
        check(move_to_uci(result.best_move) == "f3f7", "full search finds Qxf7 mate");
        check(result.display_score().is_mate, "and reports it as a mate");
    }

    void test_limits() {
        using namespace chess;
        const Position middlegame =
            parse("r1bq1rk1/ppp2ppp/2n2n2/3pp3/1bP5/2NP1NP1/PP2PPBP/R1BQ1RK1 w - - 0 1");

        SearchOptions timed = SearchOptions::preset("full", 40);
        timed.time_limit_ms = 100;
        Searcher searcher(timed);
        const auto start = std::chrono::steady_clock::now();
        const SearchResult result = searcher.search(middlegame);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        check(ms < 400, "a 100 ms limit ends the search in time (" + std::to_string(ms) + " ms)");
        check(result.stopped, "the result is marked as stopped");
        check(is_legal(middlegame, result.best_move),
              "a stopped search still returns a legal move");
        check(result.depth >= 3, "a stopped search completed some iterations");

        SearchOptions bounded = SearchOptions::preset("full", 40);
        bounded.node_limit = 50000;
        Searcher by_nodes(bounded);
        const SearchResult by_node_limit = by_nodes.search(middlegame);
        check(by_node_limit.stats.nodes < 60000, "a node limit stops the search near the budget");

        // stop() from another thread.
        SearchOptions endless = SearchOptions::preset("full", 60);
        Searcher interrupted(endless);
        std::thread stopper([&interrupted] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            interrupted.stop();
        });
        const SearchResult stopped = interrupted.search(middlegame);
        stopper.join();
        check(stopped.stopped && is_legal(middlegame, stopped.best_move),
              "stop() from another thread ends the search with a legal move");
    }

    void test_repetition() {
        using namespace chess;

        // White is a rook up but the history already holds the position twice, so any
        // move that repeats it again is a draw and the search should prefer progress.
        Position pos = parse("6k1/8/8/8/8/8/R7/6K1 w - - 0 1");
        std::vector<uint64_t> history;
        const char *shuffle[] = {"a2a3", "g8f8", "a3a2", "f8g8"};
        Position walk = pos;
        history.push_back(walk.key());

        for (int round = 0; round < 2; ++round) {
            for (const char *uci : shuffle) {
                walk.make_move(move_from_uci(walk, uci));
                history.push_back(walk.key());
            }
        }

        history.pop_back();  // `walk` is the root, which the searcher pushes itself
        Searcher searcher(SearchOptions::preset("full", 6));
        const SearchResult result = searcher.search(walk, history);
        check(is_legal(walk, result.best_move), "a move is found with repetitions in the history");
    }

}  // namespace

int main() {
    test_terminal_positions();
    test_results_are_legal();
    test_value_preserving_techniques();
    test_limits();
    test_repetition();

    if (failures == 0) {
        std::printf("all search tests passed\n");
        return 0;
    }

    std::printf("%d failures\n", failures);
    return 1;
}

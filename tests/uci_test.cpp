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

// UCI tests: scripted sessions through the engine, with the replies checked.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

// GCC 12 reports a false -Wmaybe-uninitialized from inside <regex> in the sanitizer build.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <regex>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <sstream>
#include <string>
#include <vector>

#include "movegen.hpp"
#include "uci.hpp"
#include "version.hpp"

namespace {

    int failures = 0;

    void check(bool condition, const std::string &what) {
        if (!condition) {
            ++failures;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    bool contains(const std::string &text, const std::string &part) {
        return text.find(part) != std::string::npos;
    }

    std::string session(const std::string &commands, double *seconds = nullptr) {
        std::istringstream in(commands);
        std::ostringstream out;
        const auto start = std::chrono::steady_clock::now();
        chess::uci::Engine engine(in, out);
        engine.run();

        if (seconds != nullptr) {
            *seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }
        return out.str();
    }

    size_t count(const std::string &text, const std::string &part) {
        size_t found = 0;

        for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) {
            ++found;
        }
        return found;
    }

    /**
     * The move of the only `bestmove` of a session, or an empty string.
     */
    std::string best_move(const std::string &output) {
        std::smatch match;
        return std::regex_search(output, match, std::regex("bestmove (\\S+)")) ? match[1].str()
                                                                               : "";
    }

    void test_handshake() {
        const std::string out = session("uci\nisready\nquit\n");
        check(contains(out, std::string("id name Meridian ") + chess::kEngineVersion),
              "the name carries the version");
        check(contains(out, "id author"), "an author is named");
        check(contains(out, "option name Hash type spin"), "Hash is an option");
        check(contains(out, "option name Threads type spin"), "Threads is an option");
        check(contains(out, "option name MoveOverhead type spin"), "MoveOverhead is an option");
        check(contains(out, "option name OwnBook type check"), "OwnBook is an option");
        check(contains(out, "option name Ponder type check"), "Ponder is an option");
        check(contains(out, "option name SyzygyPath type string"), "SyzygyPath is an option");
        check(contains(out, "option name Config type string default network"),
              "Config is an option with the default network");
        check(contains(out, "option name ExperienceFile type string"),
              "ExperienceFile is an option");
        check(contains(out, "uciok"), "uciok ends the identity");
        check(contains(out, "readyok"), "isready is answered");
    }

    void test_search_replies() {
        const std::string out = session("position startpos moves e2e4 e7e5\ngo depth 5\n");
        check(contains(out, "info depth 1 "), "an info line for depth 1");
        check(contains(out, "info depth 5 "), "an info line for depth 5");
        check(contains(out, " score cp "), "a centipawn score");
        check(count(out, "bestmove ") == 1, "exactly one bestmove");

        chess::Position position;
        position.make_move(chess::move_from_uci(position, "e2e4"));
        position.make_move(chess::move_from_uci(position, "e7e5"));
        check(!chess::move_from_uci(position, best_move(out)).is_null(),
              "the best move is legal: " + best_move(out));
    }

    void test_mate_is_reported() {
        const std::string out =
            session("position fen 6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1\ngo depth 4\n");
        check(best_move(out) == "a1a8", "the mate in one is played");
        check(contains(out, "score mate 1"), "the mate is reported in moves");
    }

    void test_no_move() {
        const std::string out =
            session("position fen 7k/5Q2/6K1/8/8/8/8/8 b - - 0 1\ngo depth 3\n");
        check(best_move(out) == "0000", "a position without moves answers 0000");
    }

    void test_limits() {
        double seconds = 0;
        std::string out = session("position startpos\ngo movetime 150\n", &seconds);
        check(!best_move(out).empty(), "a move time search answers");
        check(seconds < 0.6,
              "a move time of 150ms ends in time (" + std::to_string(seconds) + "s)");

        // 2 seconds on the clock with no increment: about 66ms is the budget.
        out = session("position startpos\ngo wtime 2000 btime 2000 winc 0 binc 0\n", &seconds);
        check(!best_move(out).empty(), "a clock search answers");
        check(seconds < 0.7, "a clock of 2s ends quickly (" + std::to_string(seconds) + "s)");

        out = session("position startpos\ngo nodes 5000\n");
        check(!best_move(out).empty(), "a node limit search answers");
    }

    void test_options() {
        const std::string out =
            session("setoption name Hash value 32\nsetoption name Threads value 2\n"
                    "setoption name MoveOverhead value 100\nucinewgame\nposition startpos\n"
                    "go depth 6\n");
        check(count(out, "bestmove ") == 1, "a search with two threads and 32MB answers");
        check(!best_move(out).empty() && best_move(out) != "0000", "the move is real");
    }

    void test_book() {
        const std::string off = session("position startpos\ngo movetime 50\n");
        check(!contains(off, "book move"), "the book is off by default");

        const std::string on =
            session("setoption name OwnBook value true\nposition startpos\ngo movetime 50\n");
        check(contains(on, "info string book move"), "the book answers when it is on");
        check(count(on, "bestmove ") == 1, "one bestmove from the book");

        chess::Position position;
        check(!chess::move_from_uci(position, best_move(on)).is_null(), "the book move is legal");
    }

    void test_tablebases() {
        // The tables of the test data cover queen and rook endings with up to 4 pieces.
        const std::string out =
            session("setoption name SyzygyPath value tests/data/syzygy\n"
                    "position fen 4k3/8/8/8/8/8/8/3QK3 w - - 0 1\ngo depth 6\n");
        check(contains(out, "info string Syzygy tablebases for up to 4 pieces") ||
                  contains(out, "info string Syzygy tablebases for up to 5 pieces"),
              "the tables are found");
        check(contains(out, "tbhits "), "the tables decide the position");
        check(!best_move(out).empty() && best_move(out) != "0000", "a move from the tables");

        const std::string none = session("setoption name SyzygyPath value /nonexistent\nisready\n");
        check(contains(none, "no Syzygy tablebases found"), "a path without tables is reported");
        session("setoption name SyzygyPath value <empty>\n");
    }

    void test_bad_input() {
        const std::string out = session(
            "position fen not-a-fen\nposition startpos moves e2e5\nfoo bar\n\nisready\nquit\n");
        check(contains(out, "info string invalid position"), "an invalid FEN is reported");
        check(contains(out, "info string illegal move"), "an illegal move is reported");
        check(contains(out, "readyok"), "the engine keeps going after bad input");
        check(contains(out, "Unknown command: 'foo bar'"), "an unknown command is reported");
    }

    void test_help() {
        const std::string out = session("help\nquit\n");
        check(contains(out, "Usage:") && contains(out, "Hash"), "help describes the program");
    }

    void test_perft() {
        const std::string out = session("position startpos\ngo perft 3\nquit\n");
        check(contains(out, "Nodes searched: 8902"), "perft 3 from the start is 8902");
        check(contains(out, "e2e4: 600"), "perft lists the moves of the root");
    }

    void test_search_moves() {
        const std::string out =
            session("position startpos\ngo depth 5 searchmoves a2a3 h2h3\nquit\n");
        const std::string move = best_move(out);
        check(move == "a2a3" || move == "h2h3", "the search keeps to the moves that it is given");
    }

    void test_mate_limit() {
        const std::string out =
            session("position fen 6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1\ngo mate 1\n");
        check(best_move(out) == "a1a8", "go mate 1 finds the mate in one");
        check(contains(out, "score mate 1"), "the mate score is reported");
    }

    void test_ponder() {
        const std::string out = session(
            "position startpos moves e2e4 e7e5\ngo ponder wtime 2000 btime 2000\nponderhit\n");
        check(count(out, "bestmove ") == 1, "a ponder search that is hit answers once");

        chess::Position position;
        position.make_move(chess::move_from_uci(position, "e2e4"));
        position.make_move(chess::move_from_uci(position, "e7e5"));
        check(!chess::move_from_uci(position, best_move(out)).is_null(), "the move is legal");

        // A position without a legal move is answered at once, and a late `ponderhit` adds nothing.
        const std::string mated = session("position fen 7k/5Q2/6K1/8/8/8/8/8 b - - 0 1\ngo ponder "
                                          "wtime 1000 btime 1000\nponderhit\n");
        check(count(mated, "bestmove ") == 1,
              "ponderhit after a position without a move is ignored");

        const std::string stopped =
            session("position startpos\ngo ponder wtime 2000 btime 2000\nstop\nponderhit\n");
        check(count(stopped, "bestmove ") == 1,
              "a stop ends a ponder search, and a late hit is ignored");
    }

    void test_stop() {
        // The input holds the whole session, so `stop` follows `go infinite` at once.
        double seconds = 0;
        const std::string out =
            session("position startpos\ngo infinite\nstop\nisready\n", &seconds);
        check(count(out, "bestmove ") == 1, "an infinite search answers once after stop");
        check(!best_move(out).empty(), "the stopped search has a move");
        check(seconds < 2.0, "stop ends the search quickly");
        check(contains(out, "readyok"), "the engine answers after the stop");
    }

    /**
     * The nodes that `bench 4` reports after the given commands, or an empty string.
     */
    std::string bench_nodes(const std::string &setup) {
        const std::string out = session(setup + "bench 4\nquit\n");
        const std::regex nodes("Nodes searched  : (\\d+)");
        std::smatch found;
        return std::regex_search(out, found, nodes) ? found[1].str() : "";
    }

    void test_config() {
        const char *presets[] = {"baseline", "ordering", "tt",    "quiescence", "iterative",
                                 "pruning",  "full",     "tuned", "network"};

        for (const std::string preset : presets) {
            const std::string out =
                session("setoption name Config value " + preset + "\nisready\n");
            check(contains(out, "info string Config: " + preset + "\n") &&
                      !contains(out, "invalid Config"),
                  "Config accepts the preset " + preset);
        }

        for (const char *change :
             {"network+razoring=1", "network+razoring", "tuned+see=0", "network+rfp_margin=100",
              "full+delta_pruning+razoring", "network+futility_base=-5"}) {
            const std::string out =
                session(std::string("setoption name Config value ") + change + "\nisready\n");
            check(!contains(out, "invalid Config"), std::string("Config accepts ") + change);
        }

        for (const char *bad :
             {"nonsense", "", "Network", "network+", "network+razorin", "network+rfp_margin",
              "network+see=2", "network+rfp_margin=x", "network+rfp_margin=", "network+threads=2",
              "network++see", "network+see=-1"}) {
            const std::string out =
                session(std::string("setoption name Config value ") + bad + "\nisready\n");
            check(contains(out, "info string invalid Config: "),
                  std::string("Config refuses '") + bad + "'");
            check(!contains(out, "info string Config: "),
                  std::string("Config does not apply '") + bad + "'");
        }

        const std::string network = bench_nodes("");
        const std::string baseline = bench_nodes("setoption name Config value baseline\n");
        check(!network.empty() && !baseline.empty(), "bench runs under both configurations");
        check(network != baseline, "the configurations search different numbers of nodes");
        check(baseline == bench_nodes("setoption name Config value baseline\n"),
              "a configuration gives the same nodes twice");

        // A refused Config leaves the one before it.
        check(baseline == bench_nodes("setoption name Config value baseline\n"
                                      "setoption name Config value network+razorin\n"),
              "a refused Config keeps the previous one");
        // A change after the name is applied on top of it.
        check(bench_nodes("setoption name Config value network+null_move=0\n") != network,
              "a switch changes the search");
        // A later Config replaces an earlier one, and Hash and Threads survive it.
        check(network == bench_nodes("setoption name Config value baseline\n"
                                     "setoption name Config value network\n"),
              "a later Config replaces an earlier one");

        const std::string kept =
            session("setoption name Hash value 32\nsetoption name Threads value 2\n"
                    "setoption name Config value pruning\nucinewgame\nposition startpos\n"
                    "go depth 6\n");
        check(count(kept, "bestmove ") == 1 && !best_move(kept).empty(),
              "a search answers after Config follows Hash and Threads");

        // The hand written evaluation of an early preset stays when the network is cleared.
        const std::string cleared = session("setoption name Config value baseline\n"
                                            "setoption name EvalFile value <empty>\n");
        check(contains(cleared, "info string evaluation: hand written"),
              "an early preset keeps the hand written evaluation");
    }

    struct ExperienceRow {
        uint64_t key;
        uint16_t move;
        int16_t score;
        uint16_t depth;
    };

    std::string write_experience(const std::string &name, const std::vector<ExperienceRow> &rows) {
        const std::string path = (std::filesystem::temp_directory_path() / name).string();
        std::ofstream out(path, std::ios::binary);
        const uint64_t count = rows.size();
        out.write("MRDEXP01", 8);
        out.write(reinterpret_cast<const char *>(&count), 8);

        for (const ExperienceRow &row : rows) {
            out.write(reinterpret_cast<const char *>(&row.key), 8);
            out.write(reinterpret_cast<const char *>(&row.move), 2);
            out.write(reinterpret_cast<const char *>(&row.score), 2);
            out.write(reinterpret_cast<const char *>(&row.depth), 2);
        }

        return path;
    }

    void test_experience() {
        chess::Position start;
        const chess::Move odd = chess::move_from_uci(start, "a2a3");
        const std::string deep =
            write_experience("meridian_experience_deep.bin", {{start.key(), odd.data, 25, 99}});
        const std::string shallow =
            write_experience("meridian_experience_shallow.bin", {{start.key(), odd.data, 25, 1}});
        const std::string unordered =
            write_experience("meridian_experience_unordered.bin",
                             {{start.key() + 1, odd.data, 0, 9}, {start.key(), odd.data, 0, 9}});

        const std::string none = session("position startpos\ngo depth 4\n");
        check(!contains(none, "experience move"), "no experience is used by default");

        const std::string used = session("setoption name ExperienceFile value " + deep +
                                         "\nposition startpos\ngo depth 4\n");
        check(contains(used, "info string experience: 1 positions from"),
              "the experience file is reported when it loads");
        check(contains(used, "info string experience move") && best_move(used) == "a2a3",
              "a deeper analysis decides the move");

        const std::string search_deeper = session("setoption name ExperienceFile value " + shallow +
                                                  "\nposition startpos\ngo depth 4\n");
        check(!contains(search_deeper, "experience move"),
              "a search that is as deep as the analysis keeps its own move");

        const std::string moved = session("setoption name ExperienceFile value " + deep +
                                          "\nposition startpos moves e2e4\ngo depth 4\n");
        check(!contains(moved, "experience move"),
              "a position that is not in the file is searched");

        const std::string cleared = session("setoption name ExperienceFile value " + deep +
                                            "\nsetoption name ExperienceFile value <empty>\n"
                                            "position startpos\ngo depth 4\n");
        check(contains(cleared, "info string experience: none") &&
                  !contains(cleared, "experience move"),
              "an empty value switches the experience off");

        const std::string kept = session("setoption name ExperienceFile value " + deep +
                                         "\nsetoption name Config value network\n"
                                         "position startpos\ngo depth 4\n");
        check(contains(kept, "info string experience move"), "a new Config keeps the experience");

        const std::string missing = session("setoption name ExperienceFile value /no/such/file\n");
        check(contains(missing, "cannot read an experience file"), "a missing file is refused");

        const std::string bad = session("setoption name ExperienceFile value " + unordered + "\n");
        check(contains(bad, "cannot read an experience file"),
              "a file with keys out of order is refused");

        for (const std::string &path : {deep, shallow, unordered}) {
            std::filesystem::remove(path);
        }
    }

    void test_bench() {
        std::string first = session("bench 5\nquit\n");
        std::string second = session("bench 5\nquit\n");
        const std::regex nodes("Nodes searched  : (\\d+)");
        std::smatch a;
        std::smatch b;
        check(std::regex_search(first, a, nodes) && std::regex_search(second, b, nodes),
              "bench prints the nodes searched");
        check(a[1].str() == b[1].str() && a[1].str() != "0", "bench gives the same nodes twice");
    }

}  // namespace

int main() {
    test_handshake();
    test_search_replies();
    test_mate_is_reported();
    test_no_move();
    test_limits();
    test_options();
    test_book();
    test_tablebases();
    test_bad_input();
    test_stop();
    test_bench();
    test_experience();
    test_config();
    test_help();
    test_perft();
    test_search_moves();
    test_mate_limit();
    test_ponder();

    if (failures == 0) {
        std::printf("all uci tests passed\n");
        return 0;
    }

    std::printf("%d failures\n", failures);
    return 1;
}

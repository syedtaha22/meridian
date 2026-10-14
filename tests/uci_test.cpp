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
#include <regex>
#include <sstream>
#include <string>

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

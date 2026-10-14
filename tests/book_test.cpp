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

// Book tests: the built-in book gives legal moves, covers the first moves of a game and varies.
#include <cstdio>
#include <set>
#include <string>

#include "book.hpp"
#include "movegen.hpp"

namespace {

    int failures = 0;

    void check(bool condition, const std::string &what) {
        if (!condition) {
            ++failures;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    bool is_legal(const chess::Position &pos, chess::Move move) {
        chess::MoveList list;
        chess::generate_legal(pos, list);

        for (const chess::Move candidate : list) {
            if (candidate == move) {
                return true;
            }
        }
        return false;
    }

    void test_start_position() {
        const chess::Book &book = chess::Book::builtin();
        check(book.size() > 0, "the book has entries");

        const chess::Position start;
        const auto entries = book.find(start.key());
        check(!entries.empty(), "the start position is in the book");

        std::set<std::string> moves;

        for (uint64_t random = 0; random < 200; ++random) {
            const auto choice = book.pick(start, random * 2654435761u);
            check(choice.has_value(), "the start position gives a move");

            if (choice) {
                check(is_legal(start, choice->move), "the first move is legal");
                moves.insert(chess::move_to_uci(choice->move));
            }
        }

        check(moves.size() >= 2, "the first move varies");

        for (const std::string &move : moves) {
            check(move == "e2e4" || move == "d2d4" || move == "c2c4" || move == "g1f3",
                  "the first move is one of the four allowed: " + move);
        }
    }

    /**
     * Plays book moves for both sides with different random numbers. Every move has to be
     * legal, and a game should stay in the book for several moves.
     */
    void test_walks() {
        const chess::Book &book = chess::Book::builtin();
        std::set<std::string> lines;
        int deepest = 0;

        for (uint64_t seed = 1; seed <= 300; ++seed) {
            chess::Position pos;
            std::string line;
            int plies = 0;

            for (; plies < 40; ++plies) {
                const auto choice = book.pick(pos, seed * 0x9e3779b97f4a7c15ull + plies);

                if (!choice) {
                    break;
                }

                check(is_legal(pos, choice->move), "a book move is legal");
                check(pos.make_move(choice->move), "a book move can be played");
                line += chess::move_to_uci(choice->move) + " ";
            }

            deepest = std::max(deepest, plies);
            lines.insert(line);
        }

        check(deepest >= 6, "a game stays in the book for at least 6 plies");
        check(lines.size() >= 20, "games differ: " + std::to_string(lines.size()) + " lines");
    }

}  // namespace

int main() {
    test_start_position();
    test_walks();

    if (failures == 0) {
        std::printf("all book tests passed\n");
        return 0;
    }

    std::printf("%d failures\n", failures);
    return 1;
}

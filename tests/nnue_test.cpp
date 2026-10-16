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

// Network tests: the accumulator that follows a game equals the one computed from the board, also
// when a king changes its bucket or the mirror, the evaluation is the same from both sides, the
// output layer follows the number of pieces, and a saved network loads back.
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include "movegen.hpp"
#include "nnue.hpp"
#include "search.hpp"

namespace {

    int failures = 0;

    void check(bool condition, const std::string &what) {
        if (!condition) {
            ++failures;
            std::printf("FAIL: %s\n", what.c_str());
        }
    }

    chess::nnue::Network random_network(uint64_t seed) {
        std::mt19937_64 random(seed);
        std::uniform_int_distribution<int> small(-40, 40);
        chess::nnue::Network net;

        for (auto &row : net.feature_weights) {
            for (int16_t &w : row) {
                w = static_cast<int16_t>(small(random));
            }
        }

        for (int16_t &b : net.feature_bias) {
            b = static_cast<int16_t>(small(random));
        }

        for (auto &layer : net.output_weights) {
            for (auto &row : layer) {
                for (int16_t &w : row) {
                    w = static_cast<int16_t>(small(random));
                }
            }
        }

        for (int32_t &b : net.output_bias) {
            b = small(random) * 100;
        }

        return net;
    }

    bool same(const chess::nnue::Accumulator &a, const chess::nnue::Accumulator &b) {
        if (a.count != b.count) {
            return false;
        }

        for (int side = 0; side < 2; ++side) {
            if (a.kb[side] != b.kb[side] || a.mir[side] != b.mir[side]) {
                return false;
            }

            for (int i = 0; i < chess::nnue::kHidden; ++i) {
                if (a.values[side][i] != b.values[side][i]) {
                    return false;
                }
            }
        }
        return true;
    }

    void test_incremental() {
        const chess::nnue::Network net = random_network(7);
        std::mt19937_64 random(11);
        int checked = 0;
        int castles = 0;
        int en_passant = 0;
        int promotions = 0;

        for (int game = 0; game < 60; ++game) {
            chess::Position pos;
            chess::nnue::Accumulator acc;
            chess::nnue::refresh(net, pos, acc);

            for (int ply = 0; ply < 160; ++ply) {
                chess::MoveList legal;
                chess::generate_legal(pos, legal);

                if (legal.empty() || pos.halfmove_clock() >= 100) {
                    break;
                }

                const chess::Move move =
                    legal[static_cast<int>(random() % static_cast<uint64_t>(legal.size()))];
                castles += move.is_castle();
                en_passant += move.is_en_passant();
                promotions += move.is_promotion();

                chess::nnue::Accumulator next;
                chess::nnue::update(net, pos, move, acc, next);
                pos.make_move(move);

                chess::nnue::Accumulator fresh;
                chess::nnue::refresh(net, pos, fresh);
                check(same(next, fresh), "the updated accumulator equals the refreshed one");
                acc = next;
                ++checked;
            }
        }

        check(checked > 3000, "enough positions were checked: " + std::to_string(checked));
        check(castles > 0 && promotions > 0,
              "the games had castling and promotion: " + std::to_string(castles) + ", " +
                  std::to_string(promotions));
        (void)en_passant;
    }

    /**
     * The special moves one by one, since random games seldom reach them.
     */
    void test_special_moves() {
        const chess::nnue::Network net = random_network(13);

        const struct {
            const char *fen;
            const char *move;
            const char *what;
        } cases[] = {
            {"rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "d4e3",
             "en passant by Black"},
            {"rnbqkbnr/pppp1ppp/8/3Pp3/8/8/PPP1PPPP/RNBQKBNR w KQkq e6 0 1", "d5e6",
             "en passant by White"},
            {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1", "castling kingside by White"},
            {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1c1", "castling queenside by White"},
            {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8g8", "castling kingside by Black"},
            {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8", "castling queenside by Black"},
            {"1n2k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7b8q", "promotion with a capture"},
            {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8n", "an underpromotion"},
            {"4k3/8/8/8/8/8/p7/4K3 b - - 0 1", "a2a1r", "promotion by Black"},
        };

        for (const auto &c : cases) {
            const auto pos = chess::Position::from_fen(c.fen);
            check(pos.has_value(), std::string("parses: ") + c.what);

            if (!pos) {
                continue;
            }

            const chess::Move move = chess::move_from_uci(*pos, c.move);
            check(!move.is_null(), std::string("legal: ") + c.what);

            if (move.is_null()) {
                continue;
            }

            chess::nnue::Accumulator before;
            chess::nnue::Accumulator after;
            chess::nnue::refresh(net, *pos, before);
            chess::nnue::update(net, *pos, move, before, after);
            chess::Position next = *pos;
            next.make_move(move);
            chess::nnue::Accumulator fresh;
            chess::nnue::refresh(net, next, fresh);
            check(same(after, fresh), std::string("the accumulator after ") + c.what);
        }
    }

    /**
     * The king bucket, the mirror and the output layer from the numbers that define them.
     */
    void test_contexts() {
        int kb = -1;
        int mir = -1;
        chess::nnue::king_context(0, chess::make_square(4, 0), kb, mir);
        check(kb == 0 && mir == 1, "a white king on e1 is on the low half, mirrored");
        chess::nnue::king_context(0, chess::make_square(0, 7), kb, mir);
        check(kb == 1 && mir == 0, "a white king on a8 is on the high half, not mirrored");
        chess::nnue::king_context(1, chess::make_square(4, 7), kb, mir);
        check(kb == 0 && mir == 1, "a black king on e8 sees the board flipped, like e1");
        chess::nnue::king_context(0, chess::make_square(3, 3), kb, mir);
        check(kb == 0 && mir == 0, "a white king on d4 is on the low half, not mirrored");
        chess::nnue::king_context(0, chess::make_square(4, 4), kb, mir);
        check(kb == 1 && mir == 1, "a white king on e5 is on the high half, mirrored");

        check(chess::nnue::bucket_of(2) == 0 && chess::nnue::bucket_of(5) == 0 &&
                  chess::nnue::bucket_of(6) == 1 && chess::nnue::bucket_of(9) == 1 &&
                  chess::nnue::bucket_of(32) == chess::nnue::kBuckets - 1,
              "the output layer follows the number of pieces");
    }

    /**
     * Games of two kings and a few pieces, where the kings cross the middle of the board and the
     * accumulator has to be computed again for the point of view of a king that changes bucket.
     */
    void test_king_moves() {
        const chess::nnue::Network net = random_network(21);
        std::mt19937_64 random(5);
        int changes = 0;
        int checked = 0;

        for (int game = 0; game < 40; ++game) {
            auto pos = chess::Position::from_fen("8/8/3k4/8/4K3/8/2P3p1/1N6 w - - 0 1");
            check(pos.has_value(), "the king test position parses");

            if (!pos) {
                return;
            }

            chess::nnue::Accumulator acc;
            chess::nnue::refresh(net, *pos, acc);

            for (int ply = 0; ply < 120; ++ply) {
                chess::MoveList legal;
                chess::generate_legal(*pos, legal);

                if (legal.empty() || pos->halfmove_clock() >= 100) {
                    break;
                }

                const chess::Move move =
                    legal[static_cast<int>(random() % static_cast<uint64_t>(legal.size()))];
                chess::nnue::Accumulator next;
                chess::nnue::update(net, *pos, move, acc, next);
                pos->make_move(move);
                chess::nnue::Accumulator fresh;
                chess::nnue::refresh(net, *pos, fresh);
                check(same(next, fresh), "the accumulator after a king move equals the refreshed");

                for (int side = 0; side < 2; ++side) {
                    changes += next.kb[side] != acc.kb[side] || next.mir[side] != acc.mir[side];
                }

                acc = next;
                ++checked;
            }
        }

        check(changes > 50, "the kings changed bucket or mirror often: " + std::to_string(changes));
        check(checked > 1000, "enough king positions were checked: " + std::to_string(checked));
    }

    void test_symmetry() {
        // A position and its mirror (colors exchanged, board flipped, other side to move) have
        // the same score for the side to move.
        const chess::nnue::Network net = random_network(3);
        const auto white = chess::Position::from_fen(
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        const auto black = chess::Position::from_fen(
            "r3k2r/pppbbppp/2n2q1P/1P2p3/3pn3/BN2PNP1/P1PPQPB1/R3K2R b KQkq - 0 1");
        check(white && black, "positions parse");
        chess::nnue::Accumulator a;
        chess::nnue::Accumulator b;
        chess::nnue::refresh(net, *white, a);
        chess::nnue::refresh(net, *black, b);
        check(chess::nnue::evaluate(net, a, chess::kWhite) ==
                  chess::nnue::evaluate(net, b, chess::kBlack),
              "the evaluation is the same for a position and its mirror");
    }

    void test_io() {
        const chess::nnue::Network net = random_network(5);
        const std::string path = "/tmp/meridian_nnue_test.nnue";
        check(chess::nnue::save(net, path), "a network is written");
        const auto loaded = chess::nnue::load(path);
        check(loaded != nullptr, "a network is read");

        if (loaded) {
            check(loaded->output_bias[3] == net.output_bias[3] &&
                      loaded->feature_weights[100][5] == net.feature_weights[100][5] &&
                      loaded->feature_weights[1000][5] == net.feature_weights[1000][5] &&
                      loaded->output_weights[5][1][77] == net.output_weights[5][1][77],
                  "the weights are the same after a round trip");
        }

        check(chess::nnue::load("/nonexistent/file.nnue") == nullptr, "a missing file is null");
        std::remove(path.c_str());
    }

    void test_search() {
        // A search with a network uses the accumulators along the line, and gives the same
        // result as a search that refreshes them: here, that it returns legal moves and that
        // two searches agree.
        auto net = std::make_shared<chess::nnue::Network>(random_network(9));
        chess::SearchOptions options = chess::SearchOptions::preset("tuned", 5);
        options.hash_mb = 8;
        options.nnue = net;
        chess::Searcher a(options);
        chess::Searcher b(options);
        const auto pos = chess::Position::from_fen(
            "r1bq1rk1/ppp2ppp/2n2n2/3pp3/1bP5/2NP1NP1/PP2PPBP/R1BQ1RK1 w - - 0 1");
        const chess::SearchResult first = a.search(*pos);
        const chess::SearchResult second = b.search(*pos);
        check(!first.best_move.is_null() && first.best_move == second.best_move &&
                  first.score == second.score,
              "two searches with the network agree");

        chess::MoveList legal;
        chess::generate_legal(*pos, legal);
        bool found = false;

        for (const chess::Move move : legal) {
            found = found || move == first.best_move;
        }
        check(found, "the move is legal");
    }

}  // namespace

int main() {
    test_incremental();
    test_special_moves();
    test_contexts();
    test_king_moves();
    test_symmetry();
    test_io();
    test_search();

    if (failures == 0) {
        std::printf("all nnue tests passed\n");
        return 0;
    }

    std::printf("%d failures\n", failures);
    return 1;
}

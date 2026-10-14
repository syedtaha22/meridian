// Random games: every position reached should keep the position invariants, and searching
// it should neither crash nor return an illegal move. Run under the sanitizers in CI.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "chess/eval.hpp"
#include "chess/movegen.hpp"
#include "chess/search.hpp"

namespace {

    int failures = 0;

    void check(bool condition, const std::string &what) {
        if (!condition) {
            ++failures;

            if (failures <= 20) {
                std::printf("FAIL: %s\n", what.c_str());
            }
        }
    }

    struct Random {
        uint64_t state;

        uint64_t next() {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return state;
        }
    };

}  // namespace

int main(int argc, char **argv) {
    using namespace chess;
    const int games = argc > 1 ? std::stoi(argv[1]) : 60;
    Random random{0x1234567887654321ULL};

    chess::SearchOptions options = SearchOptions::preset("full", 3);
    options.hash_mb = 4;
    Searcher searcher(options);

    long positions = 0;

    for (int game = 0; game < games; ++game) {
        Position pos;
        std::vector<uint64_t> history;

        for (int ply = 0; ply < 200; ++ply) {
            MoveList legal;
            generate_legal(pos, legal);

            if (legal.empty() || pos.halfmove_clock() >= 100 || pos.insufficient_material()) {
                break;
            }

            ++positions;

            const std::string fen = pos.to_fen();
            const auto parsed = Position::from_fen(fen);
            check(parsed.has_value(), "FEN of a reached position parses: " + fen);

            if (parsed) {
                check(parsed->key() == pos.key(), "incremental hash equals recomputed: " + fen);
                check(parsed->to_fen() == fen, "FEN round trip: " + fen);
            }

            check(perft(pos, 1) == static_cast<uint64_t>(legal.size()),
                  "perft(1) equals the legal move count: " + fen);
            check(pos.king_square(kWhite) != pos.king_square(kBlack), "two kings: " + fen);
            check(pos.pieces(kWhite) == (pos.pieces(kWhite, kPawn) | pos.pieces(kWhite, kKnight) |
                                         pos.pieces(kWhite, kBishop) | pos.pieces(kWhite, kRook) |
                                         pos.pieces(kWhite, kQueen) | pos.pieces(kWhite, kKing)),
                  "colour boards agree with piece boards: " + fen);
            check((pos.pieces(kWhite) & pos.pieces(kBlack)) == 0,
                  "no square holds two sides: " + fen);

            // Every few plies, search it; every legal move should also be playable by SAN.
            if (ply % 6 == 0) {
                const SearchResult result = searcher.search(pos, history);
                check(std::find(legal.begin(), legal.end(), result.best_move) != legal.end(),
                      "searched move is legal: " + fen);
                for (const Move m : legal) {
                    check(!move_to_san(pos, m).empty(), "SAN is produced: " + fen);
                    check(move_from_uci(pos, move_to_uci(m)) == m, "UCI round trip: " + fen);
                }
            }

            // Evaluation is antisymmetric under swapping the side to move only through the
            // colour flip, so only check that it stays inside the score range.
            check(std::abs(evaluate(pos)) < kMateBound, "evaluation within range: " + fen);

            history.push_back(pos.key());
            pos.make_move(
                legal[static_cast<int>(random.next() % static_cast<uint64_t>(legal.size()))]);
        }
    }

    std::printf("%ld positions checked\n", positions);

    if (failures == 0) {
        std::printf("all stress tests passed\n");
        return 0;
    }

    std::printf("%d failures\n", failures);
    return 1;
}

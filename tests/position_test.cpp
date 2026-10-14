// Rules tests: FEN round trips, attack tables, move generation, perft.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "eval.hpp"
#include "movegen.hpp"
#include "position.hpp"
#include "see.hpp"

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

    void test_fen() {
        const std::string start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        check(chess::Position().to_fen() == start, "default position is the initial position");
        check(parse(start).to_fen() == start, "initial FEN round trip");

        const std::string busy =
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b Kq e3 7 23";
        check(parse(busy).to_fen() == busy, "FEN with counters round trip");

        check(parse("8/8/8/8/8/8/8/K6k w KQkq - 0 1").castling() == 0,
              "castling rights without king and rook at home are dropped");
        check(parse("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq").halfmove_clock() == 0,
              "short FEN is accepted");

        check(!chess::Position::from_fen("8/8/8 w - - 0 1"), "too few ranks rejected");
        check(!chess::Position::from_fen("8/8/8/8/8/8/8/8 w - - 0 1"), "no kings rejected");
        check(!chess::Position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN w - - 0 1"),
              "short rank rejected");
        check(!chess::Position::from_fen("4k3/8/8/8/8/8/8/P3K3 w - - 0 1"),
              "pawn on first rank rejected");
        check(!chess::Position::from_fen("4k3/8/8/8/8/8/8/4K3 x - - 0 1"), "bad side rejected");

        // A king that the side to move could capture would corrupt the board in the search.
        check(!chess::Position::from_fen("4k3/4R3/8/8/8/8/8/4K3 w - - 0 1"),
              "the side not to move in check is rejected");
        check(!chess::Position::from_fen("4k3/8/8/8/8/8/8/4K3 w - e3 0 1"),
              "en passant square without a pawn is rejected");
        check(!chess::Position::from_fen("4k3/8/8/3pP3/8/8/8/4K3 w - e5 0 1"),
              "en passant square on a wrong rank is rejected");
        check(chess::Position::from_fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1").has_value(),
              "a real en passant square is accepted");
        check(!chess::Position::from_fen("4k3/8/8/8/8/8/8/4K3 w - - -1 1"),
              "negative halfmove clock rejected");
        check(!chess::Position::from_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 0"),
              "fullmove number below 1 rejected");
    }

    void test_attacks() {
        using namespace chess;

        // Magic lookups should agree with ray walking for every square and many occupancies.
        uint64_t state = 88172645463325252ULL;
        auto next = [&state]() {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return state;
        };

        bool equal = true;

        for (int i = 0; i < 20000 && equal; ++i) {
            const Bitboard occ = next() & next();

            for (Square s = 0; s < kSquares; ++s) {
                equal &= rook_attacks(s, occ) == rook_attacks_rays(s, occ);
                equal &= bishop_attacks(s, occ) == bishop_attacks_rays(s, occ);
            }
        }

        check(equal, "slider attack tables match ray walking");
        check(__builtin_popcountll(knight_attacks(0)) == 2, "corner knight has 2 moves");
        check(__builtin_popcountll(king_attacks(27)) == 8, "central king has 8 moves");
        check(pawn_attacks(kWhite, 8) == square_bb(17), "a2 pawn attacks b3 only");
    }

    void test_moves() {
        using namespace chess;
        Position start;
        MoveList list;
        generate_legal(start, list);
        check(list.size() == 20, "20 legal moves from the initial position");

        const Position promo = parse("8/P6k/8/8/8/8/8/K7 w - - 0 1");
        MoveList promos;
        generate_legal(promo, promos);
        int count = 0;

        for (const Move m : promos) {
            count += m.from() == 48 && m.is_promotion();
        }

        check(count == 4, "a pawn on the seventh rank promotes to four pieces");

        check(!move_from_uci(start, "e2e4").is_null(), "e2e4 is legal");
        check(move_from_uci(start, "e2e5").is_null(), "e2e5 is not legal");
        check(move_to_uci(move_from_uci(promo, "a7a8q")) == "a7a8q", "promotion round trip");

        // Castling needs an unattacked path, and is dropped when out of check only.
        const Position castle = parse("r3k2r/8/8/8/8/8/5r2/R3K2R w KQkq - 0 1");
        MoveList castles;
        generate_legal(castle, castles);
        bool king_side = false;
        bool queen_side = false;

        for (const Move m : castles) {
            king_side |= m.flag() == kKingCastle;
            queen_side |= m.flag() == kQueenCastle;
        }

        check(!king_side && queen_side, "cannot castle through an attacked square");

        const Position in_check = parse("4k3/8/8/8/8/8/4r3/R3K2R w KQ - 0 1");
        MoveList evasions;
        generate_legal(in_check, evasions);
        bool any_castle = false;

        for (const Move m : evasions) {
            any_castle |= m.is_castle();
        }

        check(!any_castle, "cannot castle out of check");

        const Position ep = parse("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
        check(!move_from_uci(ep, "e5d6").is_null(), "en passant capture is legal");
        Position after = ep;
        after.make_move(move_from_uci(ep, "e5d6"));
        check(after.to_fen() == "4k3/8/3P4/8/8/8/8/4K3 b - - 0 2", "en passant removes the pawn");

        // The pinned piece may not leave the pin line.
        const Position pinned = parse("4k3/8/8/8/8/8/4r3/4K1R1 w - - 0 1");
        MoveList moves;
        generate_legal(pinned, moves);
        bool rook_moves = false;

        for (const Move m : moves) {
            rook_moves |= m.from() == 6;
        }

        check(!rook_moves, "an overloaded rook cannot move while its king is in check");
    }

    void test_san() {
        using namespace chess;
        Position pos;
        check(move_to_san(pos, move_from_uci(pos, "g1f3")) == "Nf3", "knight move");
        check(move_to_san(pos, move_from_uci(pos, "e2e4")) == "e4", "pawn push");

        Position fools = pos;

        for (const char *uci : {"f2f3", "e7e5", "g2g4", "d8h4"}) {
            fools.make_move(move_from_uci(fools, uci));
        }

        MoveList none;
        generate_legal(fools, none);
        check(fools.in_check() && none.empty(), "fool's mate is checkmate");

        const Position both = parse("7k/8/8/1Q1Q4/8/8/8/3QK3 w - - 0 1");
        check(move_to_san(both, move_from_uci(both, "d5b3")) == "Qd5b3",
              "both file and rank disambiguate");
        const Position castle = parse("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        check(move_to_san(castle, move_from_uci(castle, "e1g1")) == "O-O", "king side castling");
        check(move_to_san(castle, move_from_uci(castle, "e1c1")) == "O-O-O", "queen side castling");
    }

    /**
     * `evaluate` is the tunable evaluation with its default numbers, the legacy numbers agree with
     * `evaluate_legacy` to rounding, and the evaluation is the same from both sides of the board.
     */
    void test_eval_params() {
        const chess::EvalParams params = chess::EvalParams::defaults();
        const char *fens[] = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3",
            "8/5pk1/6p1/8/2P5/1K6/8/8 w - - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        };

        for (const char *fen : fens) {
            const auto pos = chess::Position::from_fen(fen);
            check(pos.has_value(), std::string("parses ") + fen);
            check(chess::evaluate(*pos) == chess::evaluate(*pos, params),
                  "evaluate uses the default numbers");
            const int legacy = chess::evaluate_legacy(*pos);
            const int tables = chess::evaluate(*pos, chess::EvalParams::legacy());
            check(std::abs(legacy - tables) <= 2,
                  "legacy numbers match the legacy evaluation: " + std::to_string(legacy) +
                      " and " + std::to_string(tables));
        }

        // Mirrored positions score the same for the side to move once the terms are on.
        chess::EvalParams terms = params;
        terms.bishop_pair[0] = 30;
        terms.bishop_pair[1] = 40;
        terms.passed[0][5] = 40;
        terms.passed[1][5] = 80;
        terms.doubled[0] = -10;
        terms.isolated[0] = -12;
        terms.rook_open[0] = 20;
        terms.mobility[0][1] = 4;
        terms.mobility[1][3] = 3;
        terms.tempo[0] = 10;
        const auto white = chess::Position::from_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/"
                                                     "PPPBBPPP/R3K2R w KQkq - 0 1");
        const auto black = chess::Position::from_fen("r3k2r/pppbbppp/2n2q1P/1P2p3/3pn3/"
                                                     "BN2PNP1/P1PPQPB1/R3K2R b KQkq - 0 1");
        check(white && black, "mirror positions parse");
        check(chess::evaluate(*white, terms) == chess::evaluate(*black, terms),
              "the evaluation is the same for a position and its mirror");
    }

    void test_see() {
        using namespace chess;

        auto exchange = [](const char *fen, const char *uci) {
            const Position pos = parse(fen);
            return see(pos, move_from_uci(pos, uci));
        };

        check(exchange("k7/8/8/3p4/4P3/8/8/K7 w - - 0 1", "e4d5") == 100,
              "a pawn takes an undefended pawn: +100");
        check(exchange("k7/8/4p3/3p4/8/8/3Q4/K7 w - - 0 1", "d2d5") == -800,
              "a queen takes a pawn defended by a pawn: -800");
        check(exchange("k7/8/4p3/3p4/4P3/8/8/K7 w - - 0 1", "e4d5") == 0, "a pawn trade is even");
        check(exchange("k2r4/8/8/3r4/8/8/3R4/K2R4 w - - 0 1", "d2d5") == 500,
              "two rooks against a defended rook: +500");
        check(exchange("k7/8/8/3pP3/8/8/8/K7 w - d6 0 1", "e5d6") == 100,
              "en passant takes a pawn");
        check(exchange("k7/8/8/8/8/8/3n4/K2R4 w - - 0 1", "d1d2") == 320,
              "a rook takes an undefended knight");
        check(exchange("k7/8/8/8/8/2b5/3n4/3R3K w - - 0 1", "d1d2") == -180,
              "a rook takes a defended knight: 320 - 500");
    }

    void test_hash() {
        using namespace chess;

        // The incremental hash after a game should equal the hash of the final FEN.
        Position pos;

        for (const char *uci : {"e2e4", "d7d5", "e4d5", "g8f6", "f1b5", "c7c6", "d5c6", "b8c6",
                                "g1f3", "e7e5", "e1g1", "f8e7", "d2d3", "e8g8", "b5c6", "b7c6"}) {
            const Move move = move_from_uci(pos, uci);
            check(!move.is_null(), std::string("legal: ") + uci);
            pos.make_move(move);
            check(parse(pos.to_fen()).key() == pos.key(), std::string("hash after ") + uci);
        }

        // Transposition: two move orders reach the same key.
        Position a;
        Position b;

        for (const char *uci : {"g1f3", "g8f6", "b1c3", "b8c6"}) {
            a.make_move(move_from_uci(a, uci));
        }

        for (const char *uci : {"b1c3", "b8c6", "g1f3", "g8f6"}) {
            b.make_move(move_from_uci(b, uci));
        }

        check(a.key() == b.key(), "transposed move orders share a hash");
        check(Position().key() != a.key(), "different positions differ");
    }

    /**
     * perft.txt holds name, FEN and comma separated counts per line.
     */
    void test_perft(const std::string &path, int max_depth, uint64_t node_budget) {
        std::ifstream in(path);
        check(static_cast<bool>(in), "open " + path);
        std::string line;

        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            std::istringstream fields(line);
            std::string name, fen, counts;
            std::getline(fields, name, '\t');
            std::getline(fields, fen, '\t');
            std::getline(fields, counts, '\t');

            std::vector<uint64_t> expected;
            std::istringstream parts(counts);
            std::string part;

            while (std::getline(parts, part, ',')) {
                expected.push_back(std::stoull(part));
            }

            const chess::Position pos = parse(fen);

            for (int depth = 1; depth <= max_depth && depth <= static_cast<int>(expected.size());
                 ++depth) {
                if (expected[depth - 1] > node_budget) {
                    break;
                }

                const uint64_t nodes = chess::perft(pos, depth);
                check(nodes == expected[depth - 1], "perft " + name + " depth " +
                                                        std::to_string(depth) + ": got " +
                                                        std::to_string(nodes) + ", expected " +
                                                        std::to_string(expected[depth - 1]));
            }
        }
    }

}  // namespace

int main(int argc, char **argv) {
    const std::string perft_file = argc > 1 ? argv[1] : "bench/positions/perft.txt";

    test_fen();
    test_attacks();
    test_moves();
    test_san();
    test_hash();
    test_see();
    test_eval_params();
    test_perft(perft_file, 6, 200000000ULL);

    if (failures == 0) {
        std::printf("all position tests passed\n");
        return 0;
    }

    std::printf("%d failures\n", failures);
    return 1;
}

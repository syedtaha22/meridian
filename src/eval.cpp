#include "eval.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "attacks.hpp"

namespace chess {

    namespace {

        /**
         * Tables are written from White's point of view with rank 8 first, so a
         * white piece on square `s` reads entry `s ^ 56` and a black piece reads `s`.
         */
        constexpr int kPawnTable[64] = {
            0,  0,  0,   0,   0,   0,   0,  0,   //
            50, 50, 50,  50,  50,  50,  50, 50,  //
            10, 10, 20,  30,  30,  20,  10, 10,  //
            5,  5,  10,  25,  25,  10,  5,  5,   //
            0,  0,  0,   20,  20,  0,   0,  0,   //
            5,  -5, -10, 0,   0,   -10, -5, 5,   //
            5,  10, 10,  -20, -20, 10,  10, 5,   //
            0,  0,  0,   0,   0,   0,   0,  0,
        };

        constexpr int kKnightTable[64] = {
            -50, -40, -30, -30, -30, -30, -40, -50,  //
            -40, -20, 0,   0,   0,   0,   -20, -40,  //
            -30, 0,   10,  15,  15,  10,  0,   -30,  //
            -30, 5,   15,  20,  20,  15,  5,   -30,  //
            -30, 0,   15,  20,  20,  15,  0,   -30,  //
            -30, 5,   10,  15,  15,  10,  5,   -30,  //
            -40, -20, 0,   5,   5,   0,   -20, -40,  //
            -50, -40, -30, -30, -30, -30, -40, -50,
        };

        constexpr int kBishopTable[64] = {
            -20, -10, -10, -10, -10, -10, -10, -20,  //
            -10, 0,   0,   0,   0,   0,   0,   -10,  //
            -10, 0,   5,   10,  10,  5,   0,   -10,  //
            -10, 5,   5,   10,  10,  5,   5,   -10,  //
            -10, 0,   10,  10,  10,  10,  0,   -10,  //
            -10, 10,  10,  10,  10,  10,  10,  -10,  //
            -10, 5,   0,   0,   0,   0,   5,   -10,  //
            -20, -10, -10, -10, -10, -10, -10, -20,
        };

        constexpr int kRookTable[64] = {
            0,  0,  0,  0,  0,  0,  0,  0,   //
            5,  10, 10, 10, 10, 10, 10, 5,   //
            -5, 0,  0,  0,  0,  0,  0,  -5,  //
            -5, 0,  0,  0,  0,  0,  0,  -5,  //
            -5, 0,  0,  0,  0,  0,  0,  -5,  //
            -5, 0,  0,  0,  0,  0,  0,  -5,  //
            -5, 0,  0,  0,  0,  0,  0,  -5,  //
            0,  0,  0,  5,  5,  0,  0,  0,
        };

        constexpr int kQueenTable[64] = {
            -20, -10, -10, -5, -5, -10, -10, -20,  //
            -10, 0,   0,   0,  0,  0,   0,   -10,  //
            -10, 0,   5,   5,  5,  5,   0,   -10,  //
            -5,  0,   5,   5,  5,  5,   0,   -5,   //
            0,   0,   5,   5,  5,  5,   0,   -5,   //
            -10, 5,   5,   5,  5,  5,   0,   -10,  //
            -10, 0,   5,   0,  0,  0,   0,   -10,  //
            -20, -10, -10, -5, -5, -10, -10, -20,
        };

        constexpr int kKingMiddlegame[64] = {
            -30, -40, -40, -50, -50, -40, -40, -30,  //
            -30, -40, -40, -50, -50, -40, -40, -30,  //
            -30, -40, -40, -50, -50, -40, -40, -30,  //
            -30, -40, -40, -50, -50, -40, -40, -30,  //
            -20, -30, -30, -40, -40, -30, -30, -20,  //
            -10, -20, -20, -20, -20, -20, -20, -10,  //
            20,  20,  0,   0,   0,   0,   20,  20,   //
            20,  30,  10,  0,   0,   10,  30,  20,
        };

        constexpr int kKingEndgame[64] = {
            -50, -40, -30, -20, -20, -30, -40, -50,  //
            -30, -20, -10, 0,   0,   -10, -20, -30,  //
            -30, -10, 20,  30,  30,  20,  -10, -30,  //
            -30, -10, 30,  40,  40,  30,  -10, -30,  //
            -30, -10, 30,  40,  40,  30,  -10, -30,  //
            -30, -10, 20,  30,  30,  20,  -10, -30,  //
            -30, -30, 0,   0,   0,   0,   -30, -30,  //
            -50, -30, -30, -30, -30, -30, -30, -50,
        };

        constexpr const int *kTables[5] = {kPawnTable, kKnightTable, kBishopTable, kRookTable,
                                           kQueenTable};

        constexpr int kPhaseWeight[6] = {0, 1, 1, 2, 4, 0};
        constexpr int kMaxPhase = 24;

    }  // namespace

    int evaluate_legacy(const Position &pos) noexcept {
        int score[2] = {0, 0};
        int king_mg[2] = {0, 0};
        int king_eg[2] = {0, 0};
        int phase = 0;

        for (Color color : {kWhite, kBlack}) {
            for (int type = kPawn; type <= kQueen; ++type) {
                Bitboard bb = pos.pieces(color, static_cast<PieceType>(type));
                phase += kPhaseWeight[type] * __builtin_popcountll(bb);

                while (bb) {
                    const Square s = __builtin_ctzll(bb);
                    bb &= bb - 1;
                    const int index = color == kWhite ? (s ^ 56) : s;
                    score[color] += kPieceValues[type] + kTables[type][index];
                }
            }

            const Square king = pos.king_square(color);
            const int index = color == kWhite ? (king ^ 56) : king;
            king_mg[color] = kKingMiddlegame[index];
            king_eg[color] = kKingEndgame[index];
        }

        if (phase > kMaxPhase) {
            phase = kMaxPhase;
        }

        for (Color color : {kWhite, kBlack}) {
            score[color] +=
                (king_mg[color] * phase + king_eg[color] * (kMaxPhase - phase)) / kMaxPhase;
        }

        const int white_view = score[kWhite] - score[kBlack];
        return pos.side_to_move() == kWhite ? white_view : -white_view;
    }

    EvalParams EvalParams::legacy() {
        EvalParams p{};

        for (int phase = 0; phase < 2; ++phase) {
            for (int type = 0; type < 5; ++type) {
                p.material[phase][type] = kPieceValues[type];

                for (int i = 0; i < 64; ++i) {
                    p.pst[phase][type][i] = kTables[type][i];
                }
            }

            for (int i = 0; i < 64; ++i) {
                p.king[phase][i] = phase == 0 ? kKingMiddlegame[i] : kKingEndgame[i];
            }
        }

        for (int type = 0; type < 5; ++type) {
            p.phase_weight[type] = kPhaseWeight[type];
        }
        return p;
    }

    bool EvalParams::save(const std::string &path) const {
        std::ofstream out(path);

        if (!out) {
            return false;
        }

        const_cast<EvalParams *>(this)->visit([&](const char *name, int *values, int count) {
            out << name;

            for (int i = 0; i < count; ++i) {
                out << ' ' << values[i];
            }

            out << '\n';
        });
        return static_cast<bool>(out);
    }

    bool EvalParams::load(const std::string &path) {
        std::ifstream in(path);

        if (!in) {
            return false;
        }

        std::string line;

        while (std::getline(in, line)) {
            std::istringstream fields(line);
            std::string name;
            fields >> name;

            visit([&](const char *group, int *values, int count) {
                if (name != group) {
                    return;
                }

                for (int i = 0; i < count && fields; ++i) {
                    fields >> values[i];
                }
            });
        }
        return true;
    }

    namespace {

        constexpr Bitboard kFileMask(int file) {
            return Bitboard{0x0101010101010101ULL} << file;
        }

        struct PawnMasks {
            Bitboard passed[2][64];
            Bitboard adjacent[8];

            PawnMasks() {
                for (int file = 0; file < 8; ++file) {
                    adjacent[file] =
                        (file > 0 ? kFileMask(file - 1) : 0) | (file < 7 ? kFileMask(file + 1) : 0);
                }

                for (int sq = 0; sq < 64; ++sq) {
                    const int file = sq & 7;
                    const int rank = sq >> 3;
                    const Bitboard files = kFileMask(file) | adjacent[file];
                    Bitboard white_ahead = 0;
                    Bitboard black_ahead = 0;

                    for (int r = rank + 1; r < 8; ++r) {
                        white_ahead |= Bitboard{0xFFULL} << (8 * r);
                    }

                    for (int r = 0; r < rank; ++r) {
                        black_ahead |= Bitboard{0xFFULL} << (8 * r);
                    }

                    passed[kWhite][sq] = files & white_ahead;
                    passed[kBlack][sq] = files & black_ahead;
                }
            }
        };

        const PawnMasks &pawn_masks() {
            static const PawnMasks masks;
            return masks;
        }

    }  // namespace

    namespace {

        bool any_set(const int *values, int count) {
            return std::any_of(values, values + count, [](int v) { return v != 0; });
        }

    }  // namespace

    int evaluate(const Position &pos, const EvalParams &p) noexcept {
        // A term whose numbers are all zero is skipped, so that the engine does not pay for it.
        const bool use_mobility = any_set(&p.mobility[0][0], 8);
        const bool use_pawns =
            any_set(&p.passed[0][0], 16) || any_set(p.doubled, 2) || any_set(p.isolated, 2);
        const bool use_rooks = any_set(p.rook_open, 2) || any_set(p.rook_half, 2);
        const PawnMasks &masks = pawn_masks();
        const Bitboard occupied = pos.occupied();
        int mg[2] = {0, 0};
        int eg[2] = {0, 0};
        int phase = 0;

        for (Color color : {kWhite, kBlack}) {
            const Color enemy = color == kWhite ? kBlack : kWhite;
            const Bitboard own_pawns = pos.pieces(color, kPawn);
            const Bitboard enemy_pawns = pos.pieces(enemy, kPawn);
            const Bitboard own = pos.pieces(color);

            for (int type = kPawn; type <= kQueen; ++type) {
                Bitboard bb = pos.pieces(color, static_cast<PieceType>(type));
                phase += p.phase_weight[type] * __builtin_popcountll(bb);

                while (bb) {
                    const Square s = __builtin_ctzll(bb);
                    bb &= bb - 1;
                    const int index = color == kWhite ? (s ^ 56) : s;
                    mg[color] += p.material[0][type] + p.pst[0][type][index];
                    eg[color] += p.material[1][type] + p.pst[1][type][index];

                    Bitboard reach = 0;

                    switch (type) {
                    case kKnight: reach = use_mobility ? knight_attacks(s) : 0; break;
                    case kBishop: reach = use_mobility ? bishop_attacks(s, occupied) : 0; break;
                    case kRook:   {
                        reach = use_mobility ? rook_attacks(s, occupied) : 0;
                        const Bitboard file = kFileMask(s & 7);

                        if (!use_rooks) {
                            break;
                        }

                        if (!((own_pawns | enemy_pawns) & file)) {
                            mg[color] += p.rook_open[0];
                            eg[color] += p.rook_open[1];
                        } else if (!(own_pawns & file)) {
                            mg[color] += p.rook_half[0];
                            eg[color] += p.rook_half[1];
                        }
                        break;
                    }
                    case kQueen: reach = use_mobility ? queen_attacks(s, occupied) : 0; break;
                    case kPawn:  {
                        if (!use_pawns) {
                            break;
                        }

                        const int rank = color == kWhite ? (s >> 3) : 7 - (s >> 3);

                        if (!(enemy_pawns & masks.passed[color][s])) {
                            mg[color] += p.passed[0][rank];
                            eg[color] += p.passed[1][rank];
                        }
                        if (!(own_pawns & masks.adjacent[s & 7])) {
                            mg[color] += p.isolated[0];
                            eg[color] += p.isolated[1];
                        }
                        if (own_pawns & kFileMask(s & 7) &
                            (color == kWhite ? ~Bitboard{0} << (s + 1) : (Bitboard{1} << s) - 1)) {
                            mg[color] += p.doubled[0];
                            eg[color] += p.doubled[1];
                        }
                        break;
                    }
                    default: break;
                    }

                    if (use_mobility && type >= kKnight && type <= kQueen) {
                        const int count = __builtin_popcountll(reach & ~own);
                        mg[color] += p.mobility[0][type - kKnight] * count;
                        eg[color] += p.mobility[1][type - kKnight] * count;
                    }
                }
            }

            if (__builtin_popcountll(pos.pieces(color, kBishop)) >= 2) {
                mg[color] += p.bishop_pair[0];
                eg[color] += p.bishop_pair[1];
            }

            const Square king = pos.king_square(color);
            const int index = color == kWhite ? (king ^ 56) : king;
            mg[color] += p.king[0][index];
            eg[color] += p.king[1][index];
        }

        phase = std::min(phase, kMaxPhase);
        const int white_mg = mg[kWhite] - mg[kBlack];
        const int white_eg = eg[kWhite] - eg[kBlack];
        int white_view = (white_mg * phase + white_eg * (kMaxPhase - phase)) / kMaxPhase;
        const int tempo = (p.tempo[0] * phase + p.tempo[1] * (kMaxPhase - phase)) / kMaxPhase;
        white_view += pos.side_to_move() == kWhite ? tempo : -tempo;
        return pos.side_to_move() == kWhite ? white_view : -white_view;
    }

    namespace {

#include "eval_defaults.inc"

        template <size_t N> void fill(int *destination, const int (&source)[N]) {
            std::copy(source, source + N, destination);
        }

    }  // namespace

    EvalParams EvalParams::defaults() {
        EvalParams p{};
        fill(&p.material[0][0], kTuned_material);
        fill(&p.pst[0][0][0], kTuned_pst);
        fill(&p.king[0][0], kTuned_king);
        fill(&p.phase_weight[0], kTuned_phase_weight);
        fill(&p.bishop_pair[0], kTuned_bishop_pair);
        fill(&p.passed[0][0], kTuned_passed);
        fill(&p.doubled[0], kTuned_doubled);
        fill(&p.isolated[0], kTuned_isolated);
        fill(&p.rook_open[0], kTuned_rook_open);
        fill(&p.rook_half[0], kTuned_rook_half);
        fill(&p.mobility[0][0], kTuned_mobility);
        fill(&p.tempo[0], kTuned_tempo);
        return p;
    }

    int evaluate(const Position &pos) noexcept {
        static const EvalParams params = EvalParams::defaults();
        return evaluate(pos, params);
    }

}  // namespace chess

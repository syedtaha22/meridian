#include "chess/movegen.hpp"

namespace chess {

    namespace {

        constexpr Bitboard kRank2 = 0x000000000000FF00ULL;
        constexpr Bitboard kRank7 = 0x00FF000000000000ULL;
        constexpr Bitboard kFileA = 0x0101010101010101ULL;
        constexpr Bitboard kFileH = 0x8080808080808080ULL;

        inline Square pop_lsb(Bitboard &bb) {
            const Square s = __builtin_ctzll(bb);
            bb &= bb - 1;
            return s;
        }

        void add_promotions(MoveList &list, Square from, Square to, bool capture) {
            const int base = capture ? kPromoCaptureKnight : kPromoKnight;
            list.add(Move(from, to, static_cast<MoveFlag>(base + 3)));  // queen
            list.add(Move(from, to, static_cast<MoveFlag>(base + 2)));  // rook
            list.add(Move(from, to, static_cast<MoveFlag>(base + 1)));  // bishop
            list.add(Move(from, to, static_cast<MoveFlag>(base + 0)));  // knight
        }

        template <bool CapturesOnly> void generate_pawn_moves(const Position &pos, MoveList &list) {
            const Color us = pos.side_to_move();
            const Color them = ~us;
            const Bitboard pawns = pos.pieces(us, kPawn);
            const Bitboard enemies = pos.pieces(them);
            const Bitboard empty = ~pos.occupied();
            const int up = us == kWhite ? 8 : -8;
            const Bitboard promo_from = us == kWhite ? kRank7 : kRank2;
            const Bitboard start_from = us == kWhite ? kRank2 : kRank7;

            Bitboard bb = pawns;

            while (bb) {
                const Square from = pop_lsb(bb);
                const Bitboard from_bb = square_bb(from);
                const bool promoting = (from_bb & promo_from) != 0;

                // Pushes. In quiescence only queen promotions by push are worth searching.
                const Square one = from + up;

                if (empty & square_bb(one)) {
                    if (promoting) {
                        if (CapturesOnly) {
                            list.add(Move(from, one, kPromoQueen));
                        } else {
                            add_promotions(list, from, one, false);
                        }
                    } else if (!CapturesOnly) {
                        list.add(Move(from, one, kQuiet));
                        const Square two = one + up;

                        if ((from_bb & start_from) && (empty & square_bb(two))) {
                            list.add(Move(from, two, kDoublePush));
                        }
                    }
                }

                // Captures, including promotion captures.
                Bitboard targets = pawn_attacks(us, from) & enemies;

                while (targets) {
                    const Square to = pop_lsb(targets);

                    if (promoting) {
                        add_promotions(list, from, to, true);
                    } else {
                        list.add(Move(from, to, kCapture));
                    }
                }

                if (pos.en_passant() != kNoSquare &&
                    (pawn_attacks(us, from) & square_bb(pos.en_passant()))) {
                    list.add(Move(from, pos.en_passant(), kEnPassant));
                }
            }
        }

        template <bool CapturesOnly>
        void generate_piece_moves(const Position &pos, MoveList &list) {
            const Color us = pos.side_to_move();
            const Bitboard own = pos.pieces(us);
            const Bitboard enemies = pos.pieces(~us);
            const Bitboard occ = pos.occupied();
            const Bitboard allowed = CapturesOnly ? enemies : ~own;

            auto emit = [&](Square from, Bitboard targets) {
                targets &= allowed;

                while (targets) {
                    const Square to = pop_lsb(targets);
                    const bool capture = (square_bb(to) & enemies) != 0;
                    list.add(Move(from, to, capture ? kCapture : kQuiet));
                }
            };

            Bitboard bb = pos.pieces(us, kKnight);

            while (bb) {
                const Square from = pop_lsb(bb);
                emit(from, knight_attacks(from));
            }

            bb = pos.pieces(us, kBishop);

            while (bb) {
                const Square from = pop_lsb(bb);
                emit(from, bishop_attacks(from, occ));
            }

            bb = pos.pieces(us, kRook);

            while (bb) {
                const Square from = pop_lsb(bb);
                emit(from, rook_attacks(from, occ));
            }

            bb = pos.pieces(us, kQueen);

            while (bb) {
                const Square from = pop_lsb(bb);
                emit(from, queen_attacks(from, occ));
            }

            const Square king = pos.king_square(us);
            emit(king, king_attacks(king));
        }

        void generate_castling(const Position &pos, MoveList &list) {
            const Color us = pos.side_to_move();
            const Color them = ~us;
            const Bitboard occ = pos.occupied();
            const int king_side = us == kWhite ? kWhiteKingSide : kBlackKingSide;
            const int queen_side = us == kWhite ? kWhiteQueenSide : kBlackQueenSide;
            const Square home = us == kWhite ? 4 : 60;

            if (!(pos.castling() & (king_side | queen_side)) || pos.is_attacked(home, them)) {
                return;
            }
            if ((pos.castling() & king_side) &&
                !(occ & (square_bb(home + 1) | square_bb(home + 2))) &&
                !pos.is_attacked(home + 1, them) && !pos.is_attacked(home + 2, them)) {
                list.add(Move(home, home + 2, kKingCastle));
            }

            if ((pos.castling() & queen_side) &&
                !(occ & (square_bb(home - 1) | square_bb(home - 2) | square_bb(home - 3))) &&
                !pos.is_attacked(home - 1, them) && !pos.is_attacked(home - 2, them)) {
                list.add(Move(home, home - 2, kQueenCastle));
            }
        }

    }  // namespace

    void generate_pseudo_legal(const Position &pos, MoveList &list) {
        generate_pawn_moves<false>(pos, list);
        generate_piece_moves<false>(pos, list);
        generate_castling(pos, list);
    }

    void generate_captures(const Position &pos, MoveList &list) {
        generate_pawn_moves<true>(pos, list);
        generate_piece_moves<true>(pos, list);
    }

    void generate_legal(const Position &pos, MoveList &list) {
        MoveList pseudo;
        generate_pseudo_legal(pos, pseudo);

        for (const Move move : pseudo) {
            Position next = pos;

            if (next.make_move(move)) {
                list.add(move);
            }
        }
    }

    uint64_t perft(const Position &pos, int depth) {
        if (depth == 0) {
            return 1;
        }

        MoveList list;
        generate_pseudo_legal(pos, list);
        uint64_t nodes = 0;

        for (const Move move : list) {
            Position next = pos;

            if (!next.make_move(move)) {
                continue;
            }

            nodes += depth == 1 ? 1 : perft(next, depth - 1);
        }

        return nodes;
    }

    std::string move_to_uci(Move move) {
        std::string text;
        text += static_cast<char>('a' + file_of(move.from()));
        text += static_cast<char>('1' + rank_of(move.from()));
        text += static_cast<char>('a' + file_of(move.to()));
        text += static_cast<char>('1' + rank_of(move.to()));

        if (move.is_promotion()) {
            text += "nbrq"[move.promotion_type() - kKnight];
        }
        return text;
    }

    Move move_from_uci(const Position &pos, std::string_view text) {
        MoveList list;
        generate_legal(pos, list);

        for (const Move move : list) {
            if (move_to_uci(move) == text) {
                return move;
            }
        }
        return Move();
    }

    std::string move_to_san(const Position &pos, Move move) {
        const Piece piece = pos.piece_on(move.from());
        const PieceType type = piece_type(piece);
        std::string san;

        if (move.is_castle()) {
            san = move.flag() == kKingCastle ? "O-O" : "O-O-O";
        } else {
            const char file = static_cast<char>('a' + file_of(move.from()));

            if (type == kPawn) {
                if (move.is_capture()) {
                    san += file;
                    san += 'x';
                }
            } else {
                san += "PNBRQK"[type];
                MoveList legal;
                generate_legal(pos, legal);
                bool ambiguous = false;
                bool same_file = false;
                bool same_rank = false;

                for (const Move other : legal) {
                    if (other.to() == move.to() && other.from() != move.from() &&
                        pos.piece_on(other.from()) == piece) {
                        ambiguous = true;
                        same_file |= file_of(other.from()) == file_of(move.from());
                        same_rank |= rank_of(other.from()) == rank_of(move.from());
                    }
                }

                if (ambiguous) {
                    if (!same_file) {
                        san += file;
                    } else if (!same_rank) {
                        san += static_cast<char>('1' + rank_of(move.from()));
                    } else {
                        san += file;
                        san += static_cast<char>('1' + rank_of(move.from()));
                    }
                }

                if (move.is_capture()) {
                    san += 'x';
                }
            }

            san += static_cast<char>('a' + file_of(move.to()));
            san += static_cast<char>('1' + rank_of(move.to()));

            if (move.is_promotion()) {
                san += '=';
                san += "NBRQ"[move.promotion_type() - kKnight];
            }
        }

        Position next = pos;
        next.make_move(move);

        if (next.in_check()) {
            MoveList replies;
            generate_legal(next, replies);
            san += replies.empty() ? '#' : '+';
        }
        return san;
    }

}  // namespace chess

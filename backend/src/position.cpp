#include "chess/position.hpp"

#include <cctype>
#include <sstream>
#include <vector>

namespace chess {

    namespace {

        /**
         * Hash keys, generated from a fixed seed so hashes are reproducible.
         */
        struct Zobrist {
            uint64_t piece[12][kSquares];
            uint64_t castling[16];
            uint64_t en_passant_file[8];
            uint64_t side;

            Zobrist() {
                uint64_t state = 0xD1B54A32D192ED03ULL;
                auto next = [&state]() {
                    // splitmix64
                    state += 0x9E3779B97F4A7C15ULL;
                    uint64_t z = state;
                    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
                    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;

                    return z ^ (z >> 31);
                };

                for (auto &row : piece) {
                    for (uint64_t &key : row) {
                        key = next();
                    }
                }

                for (uint64_t &key : castling) {
                    key = next();
                }

                for (uint64_t &key : en_passant_file) {
                    key = next();
                }

                side = next();
            }
        };

        const Zobrist &zobrist() {
            static const Zobrist keys;
            return keys;
        }

        /**
         * Castling rights that survive a move touching each square.
         */
        struct CastlingMasks {
            int mask[kSquares];

            CastlingMasks() {
                for (int &m : mask) {
                    m = 15;
                }

                mask[0] = 15 & ~kWhiteQueenSide;
                mask[7] = 15 & ~kWhiteKingSide;
                mask[4] = 15 & ~(kWhiteKingSide | kWhiteQueenSide);
                mask[56] = 15 & ~kBlackQueenSide;
                mask[63] = 15 & ~kBlackKingSide;
                mask[60] = 15 & ~(kBlackKingSide | kBlackQueenSide);
            }
        };

        const CastlingMasks &castling_masks() {
            static const CastlingMasks masks;
            return masks;
        }

        constexpr const char *kPieceChars = "PNBRQKpnbrqk";

    }  // namespace

    Position::Position(std::nullptr_t) noexcept {
        clear();
    }

    Position::Position() : Position(nullptr) {
        *this = *from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    }

    void Position::clear() noexcept {
        for (Bitboard &bb : by_piece_) {
            bb = 0;
        }

        by_color_[0] = by_color_[1] = 0;

        for (uint8_t &p : board_) {
            p = kNoPiece;
        }

        side_ = kWhite;
        castling_ = 0;
        en_passant_ = kNoSquare;
        halfmove_ = 0;
        fullmove_ = 1;
        key_ = 0;
    }

    void Position::put(Piece piece, Square square) noexcept {
        const Bitboard bit = square_bb(square);
        by_piece_[piece] |= bit;
        by_color_[piece_color(piece)] |= bit;
        board_[square] = static_cast<uint8_t>(piece);
        key_ ^= zobrist().piece[piece][square];
    }

    void Position::remove(Piece piece, Square square) noexcept {
        const Bitboard bit = square_bb(square);
        by_piece_[piece] &= ~bit;
        by_color_[piece_color(piece)] &= ~bit;
        board_[square] = kNoPiece;
        key_ ^= zobrist().piece[piece][square];
    }

    uint64_t Position::en_passant_key() const noexcept {
        // An en passant square only distinguishes positions when a pawn can use it.
        if (en_passant_ == kNoSquare) {
            return 0;
        }

        const Bitboard capturers = pawn_attacks(~side_, en_passant_) & pieces(side_, kPawn);
        return capturers ? zobrist().en_passant_file[file_of(en_passant_)] : 0;
    }

    uint64_t Position::compute_key() const noexcept {
        uint64_t key = 0;

        for (Square s = 0; s < kSquares; ++s) {
            if (board_[s] != kNoPiece) {
                key ^= zobrist().piece[board_[s]][s];
            }
        }

        key ^= zobrist().castling[castling_];

        if (side_ == kBlack) {
            key ^= zobrist().side;
        }
        return key ^ en_passant_key();
    }

    std::optional<Position> Position::from_fen(std::string_view fen) {
        std::istringstream in{std::string(fen)};
        std::string placement, side, castling, ep;
        int halfmove = 0;
        int fullmove = 1;

        if (!(in >> placement)) {
            return std::nullopt;
        }
        if (!(in >> side)) {
            side = "w";
        }
        if (!(in >> castling)) {
            castling = "-";
        }
        if (!(in >> ep)) {
            ep = "-";
        }
        if (!(in >> halfmove)) {
            halfmove = 0;
        }
        if (!(in >> fullmove)) {
            fullmove = 1;
        }
        if (halfmove < 0 || fullmove < 1) {
            return std::nullopt;
        }

        Position pos{nullptr};

        int rank = 7;
        int file = 0;

        for (char c : placement) {
            if (c == '/') {
                if (file != 8 || rank == 0) {
                    return std::nullopt;
                }

                --rank;
                file = 0;
            } else if (c >= '1' && c <= '8') {
                file += c - '0';

                if (file > 8) {
                    return std::nullopt;
                }
            } else {
                const char *found = std::char_traits<char>::find(kPieceChars, 12, c);

                if (found == nullptr || file > 7) {
                    return std::nullopt;
                }

                pos.put(static_cast<Piece>(found - kPieceChars), make_square(file, rank));
                ++file;
            }
        }

        if (rank != 0 || file != 8) {
            return std::nullopt;
        }

        for (Color color : {kWhite, kBlack}) {
            if (__builtin_popcountll(pos.pieces(color, kKing)) != 1) {
                return std::nullopt;
            }
        }

        // Pawns on the first or last rank cannot be reached in a game.
        if (pos.by_piece_[make_piece(kWhite, kPawn)] & 0xFF000000000000FFULL ||
            pos.by_piece_[make_piece(kBlack, kPawn)] & 0xFF000000000000FFULL) {
            return std::nullopt;
        }

        if (side == "w") {
            pos.side_ = kWhite;
        } else if (side == "b") {
            pos.side_ = kBlack;
        } else {
            return std::nullopt;
        }

        for (char c : castling) {
            switch (c) {
            case 'K': pos.castling_ |= kWhiteKingSide; break;
            case 'Q': pos.castling_ |= kWhiteQueenSide; break;
            case 'k': pos.castling_ |= kBlackKingSide; break;
            case 'q': pos.castling_ |= kBlackQueenSide; break;
            case '-': break;
            default:  return std::nullopt;
            }
        }

        // Keep only rights whose king and rook are at home.
        auto at = [&pos](Square s, Color c, PieceType t) {
            return pos.board_[s] == make_piece(c, t);
        };
        if (!at(4, kWhite, kKing)) {
            pos.castling_ &= ~(kWhiteKingSide | kWhiteQueenSide);
        }
        if (!at(60, kBlack, kKing)) {
            pos.castling_ &= ~(kBlackKingSide | kBlackQueenSide);
        }
        if (!at(7, kWhite, kRook)) {
            pos.castling_ &= ~kWhiteKingSide;
        }
        if (!at(0, kWhite, kRook)) {
            pos.castling_ &= ~kWhiteQueenSide;
        }
        if (!at(63, kBlack, kRook)) {
            pos.castling_ &= ~kBlackKingSide;
        }
        if (!at(56, kBlack, kRook)) {
            pos.castling_ &= ~kBlackQueenSide;
        }

        if (ep != "-") {
            if (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h' || ep[1] < '1' || ep[1] > '8') {
                return std::nullopt;
            }

            pos.en_passant_ = make_square(ep[0] - 'a', ep[1] - '1');

            // The square must be the one behind a pawn of the side that has just moved.
            const bool white_to_move = pos.side_ == kWhite;
            const int behind = white_to_move ? -8 : 8;
            const int expected_rank = white_to_move ? 5 : 2;
            const Square target = pos.en_passant_;
            const Piece pusher = make_piece(white_to_move ? kBlack : kWhite, kPawn);

            if (rank_of(target) != expected_rank || pos.board_[target] != kNoPiece ||
                pos.board_[target + behind] != pusher) {
                return std::nullopt;
            }
        }

        if (halfmove < 0 || halfmove > 1000 || fullmove < 1) {
            return std::nullopt;
        }

        // The side that is not to move cannot be in check: its king could be captured.
        if (pos.is_attacked(pos.king_square(~pos.side_), pos.side_)) {
            return std::nullopt;
        }

        pos.halfmove_ = halfmove;
        pos.fullmove_ = fullmove;
        pos.key_ = pos.compute_key();
        return pos;
    }

    std::string Position::to_fen() const {
        std::string fen;

        for (int rank = 7; rank >= 0; --rank) {
            int empty = 0;

            for (int file = 0; file < 8; ++file) {
                const Piece piece = board_[make_square(file, rank)];

                if (piece == kNoPiece) {
                    ++empty;
                    continue;
                }
                if (empty > 0) {
                    fen += static_cast<char>('0' + empty);
                    empty = 0;
                }

                fen += kPieceChars[piece];
            }

            if (empty > 0) {
                fen += static_cast<char>('0' + empty);
            }
            if (rank > 0) {
                fen += '/';
            }
        }

        fen += side_ == kWhite ? " w " : " b ";

        if (castling_ == 0) {
            fen += '-';
        } else {
            if (castling_ & kWhiteKingSide)
                fen += 'K';
            if (castling_ & kWhiteQueenSide)
                fen += 'Q';
            if (castling_ & kBlackKingSide)
                fen += 'k';
            if (castling_ & kBlackQueenSide)
                fen += 'q';
        }

        fen += ' ';

        if (en_passant_ == kNoSquare) {
            fen += '-';
        } else {
            fen += static_cast<char>('a' + file_of(en_passant_));
            fen += static_cast<char>('1' + rank_of(en_passant_));
        }

        fen += ' ' + std::to_string(halfmove_) + ' ' + std::to_string(fullmove_);
        return fen;
    }

    bool Position::is_attacked(Square square, Color by) const noexcept {
        const Bitboard occ = occupied();

        if (pawn_attacks(~by, square) & pieces(by, kPawn)) {
            return true;
        }
        if (knight_attacks(square) & pieces(by, kKnight)) {
            return true;
        }
        if (king_attacks(square) & pieces(by, kKing)) {
            return true;
        }

        const Bitboard queens = pieces(by, kQueen);

        if (bishop_attacks(square, occ) & (pieces(by, kBishop) | queens)) {
            return true;
        }
        return (rook_attacks(square, occ) & (pieces(by, kRook) | queens)) != 0;
    }

    bool Position::make_move(Move move) noexcept {
        const Zobrist &z = zobrist();
        const Color us = side_;
        const Color them = ~us;
        const Square from = move.from();
        const Square to = move.to();
        const Piece moving = board_[from];

        key_ ^= en_passant_key();
        key_ ^= z.castling[castling_];

        const bool pawn_move = piece_type(moving) == kPawn;

        if (move.is_en_passant()) {
            const Square captured_square = to + (us == kWhite ? -8 : 8);
            remove(make_piece(them, kPawn), captured_square);
        } else if (move.is_capture()) {
            remove(static_cast<Piece>(board_[to]), to);
        }

        remove(moving, from);

        if (move.is_promotion()) {
            put(make_piece(us, move.promotion_type()), to);
        } else {
            put(moving, to);
        }

        if (move.flag() == kKingCastle) {
            const Piece rook = make_piece(us, kRook);
            remove(rook, to + 1);
            put(rook, to - 1);
        } else if (move.flag() == kQueenCastle) {
            const Piece rook = make_piece(us, kRook);
            remove(rook, to - 2);
            put(rook, to + 1);
        }

        const CastlingMasks &masks = castling_masks();
        castling_ &= masks.mask[from] & masks.mask[to];
        key_ ^= z.castling[castling_];

        en_passant_ = move.flag() == kDoublePush ? (from + to) / 2 : kNoSquare;

        halfmove_ = (pawn_move || move.is_capture()) ? 0 : halfmove_ + 1;

        if (us == kBlack) {
            ++fullmove_;
        }

        side_ = them;
        key_ ^= z.side;
        key_ ^= en_passant_key();

        return !is_attacked(king_square(us), them);
    }

    void Position::make_null_move() noexcept {
        key_ ^= en_passant_key();
        en_passant_ = kNoSquare;
        side_ = ~side_;
        key_ ^= zobrist().side;
        ++halfmove_;
    }

    bool Position::has_non_pawn_material() const noexcept {
        return (by_piece_[make_piece(side_, kKnight)] | by_piece_[make_piece(side_, kBishop)] |
                by_piece_[make_piece(side_, kRook)] | by_piece_[make_piece(side_, kQueen)]) != 0;
    }

    bool Position::insufficient_material() const noexcept {
        for (Color color : {kWhite, kBlack}) {
            if (pieces(color, kPawn) | pieces(color, kRook) | pieces(color, kQueen)) {
                return false;
            }
        }

        const Bitboard knights = pieces(kWhite, kKnight) | pieces(kBlack, kKnight);
        const Bitboard bishops = pieces(kWhite, kBishop) | pieces(kBlack, kBishop);
        const int minors = __builtin_popcountll(knights | bishops);

        if (minors <= 1) {
            return true;
        }
        if (knights == 0) {
            constexpr Bitboard kLightSquares = 0x55AA55AA55AA55AAULL;
            return (bishops & kLightSquares) == 0 || (bishops & ~kLightSquares) == 0;
        }
        return false;
    }

}  // namespace chess

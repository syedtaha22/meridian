#pragma once

#ifdef CHESS_PEXT
#include <immintrin.h>
#endif

#include "types.hpp"

namespace chess {

    namespace detail {

        /**
         * One entry of a magic bitboard table.
         */
        struct Magic {
            Bitboard mask;      ///< Squares whose occupancy changes the attacks.
            Bitboard magic;     ///< Multiplier that hashes the masked occupancy.
            Bitboard *attacks;  ///< Attack sets of this square, indexed by the hash.
            int shift;          ///< 64 minus the number of hash bits.
        };

        extern Bitboard knight_table[kSquares];
        extern Bitboard king_table[kSquares];
        extern Bitboard pawn_table[2][kSquares];
        extern Magic rook_magics[kSquares];
        extern Magic bishop_magics[kSquares];

    }  // namespace detail

    /**
     * @brief Squares attacked by a knight on `square`.
     *
     * @param square Origin square.
     * @return Attacked squares.
     */
    inline Bitboard knight_attacks(Square square) noexcept {
        return detail::knight_table[square];
    }

    /**
     * @brief Squares attacked by a king on `square`.
     *
     * @param square Origin square.
     * @return Attacked squares.
     */
    inline Bitboard king_attacks(Square square) noexcept {
        return detail::king_table[square];
    }

    /**
     * @brief Squares attacked by a pawn of `color` on `square`.
     *
     * @param color Side of the pawn.
     * @param square Origin square.
     * @return The two diagonal squares ahead of the pawn that exist.
     */
    inline Bitboard pawn_attacks(Color color, Square square) noexcept {
        return detail::pawn_table[color][square];
    }

    /**
     * @brief Rook attacks computed by walking each ray, without lookup tables.
     *
     * The first blocker in each direction is included, whatever its side.
     *
     * @param square Origin square.
     * @param occupied All pieces on the board.
     * @return Attacked squares.
     */
    Bitboard rook_attacks_rays(Square square, Bitboard occupied) noexcept;

    /**
     * @brief Bishop attacks computed by walking each ray, without lookup tables.
     *
     * @param square Origin square.
     * @param occupied All pieces on the board.
     * @return Attacked squares.
     */
    Bitboard bishop_attacks_rays(Square square, Bitboard occupied) noexcept;

    /**
     * @brief Squares a rook on `square` attacks given the blockers in `occupied`.
     *
     * Uses magic bitboards, or ray walking when built with CHESS_RAY_ATTACKS.
     *
     * @param square Origin square.
     * @param occupied All pieces on the board.
     * @return Attacked squares, including the first blocker in each direction.
     */
    inline Bitboard rook_attacks(Square square, Bitboard occupied) noexcept {
#ifdef CHESS_RAY_ATTACKS
        return rook_attacks_rays(square, occupied);
#else
        const detail::Magic &m = detail::rook_magics[square];
#ifdef CHESS_PEXT
        return m.attacks[_pext_u64(occupied, m.mask)];
#else
        return m.attacks[((occupied & m.mask) * m.magic) >> m.shift];
#endif
#endif
    }

    /**
     * @brief Squares a bishop on `square` attacks given the blockers in `occupied`.
     *
     * @param square Origin square.
     * @param occupied All pieces on the board.
     * @return Attacked squares, including the first blocker in each direction.
     */
    inline Bitboard bishop_attacks(Square square, Bitboard occupied) noexcept {
#ifdef CHESS_RAY_ATTACKS
        return bishop_attacks_rays(square, occupied);
#else
        const detail::Magic &m = detail::bishop_magics[square];
#ifdef CHESS_PEXT
        return m.attacks[_pext_u64(occupied, m.mask)];
#else
        return m.attacks[((occupied & m.mask) * m.magic) >> m.shift];
#endif
#endif
    }

    /**
     * @brief Squares a queen on `square` attacks given the blockers in `occupied`.
     *
     * @param square Origin square.
     * @param occupied All pieces on the board.
     * @return Attacked squares.
     */
    inline Bitboard queen_attacks(Square square, Bitboard occupied) noexcept {
        return rook_attacks(square, occupied) | bishop_attacks(square, occupied);
    }

}  // namespace chess

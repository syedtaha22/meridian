/**
 * Piece letter in FEN case: upper case for white, lower case for black.
 * An empty square is the empty string.
 */
export type Piece = 'P' | 'N' | 'B' | 'R' | 'Q' | 'K' | 'p' | 'n' | 'b' | 'r' | 'q' | 'k'

export type Cell = Piece | ''

/**
 * Row-major flat board of length 64 (index = row*8 + col, row 0 is rank 8, col 0 is the a
 * file), so a square converts to a UCI string without reshaping.
 */
export type Board = Cell[]

/**
 * Side to move, in FEN letters.
 */
export type Color = 'w' | 'b'

export type PromotionPiece = 'q' | 'r' | 'b' | 'n'

export interface Move {
  from: number
  to: number
  promotion?: PromotionPiece
}

export interface CastlingRights {
  K: boolean
  Q: boolean
  k: boolean
  q: boolean
}

/**
 * Everything a FEN string holds.
 */
export interface Position {
  board: Board
  turn: Color
  castling: CastlingRights

  /**
   * Square a pawn may capture onto en passant, or null.
   */
  enPassant: number | null

  /**
   * Plies since the last capture or pawn move.
   */
  halfmove: number
  fullmove: number
}

export type DrawReason = 'fifty-move' | 'repetition' | 'insufficient-material'

export type GameStatus = 'playing' | 'checkmate' | 'stalemate' | 'draw'

export interface GameState {
  position: Position

  /**
   * FEN the game started from; the move list is relative to it.
   */
  startFen: string

  /**
   * Moves played since `startFen`, oldest first.
   */
  moves: Move[]

  /**
   * The moves in algebraic notation.
   */
  sans: string[]

  /**
   * Repetition key of every position seen, starting with `startFen`.
   */
  keys: string[]
  status: GameStatus

  /**
   * Set when `status` is `draw`.
   */
  drawReason: DrawReason | null

  /**
   * Set when `status` is `checkmate`.
   */
  winner: Color | null
  inCheck: boolean
  lastMove: Move | null
}

/**
 * Engine evaluation from the side to move: centipawns, or moves to mate (negative when the side
 * to move is mated). Exactly one of the two is set.
 */
export interface Score {
  cp: number | null
  mate: number | null
}

/**
 * The limits of one search: its depth and its time.
 */
export interface EngineSettings {
  /**
   * Search depth in plies.
   */
  depth: number

  /**
   * Wall-clock budget per move in milliseconds, 0 for none.
   */
  timeLimitMs: number
}

export const DEFAULT_ENGINE_SETTINGS: EngineSettings = {
  depth: 4,
  timeLimitMs: 2000,
}

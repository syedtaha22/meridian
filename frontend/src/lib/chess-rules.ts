import type {
  Board,
  CastlingRights,
  Cell,
  Color,
  DrawReason,
  GameState,
  GameStatus,
  Move,
  Piece,
  Position,
  PromotionPiece,
} from './types'

export const START_FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'

const FILES = 'abcdefgh'
const PIECES = 'PNBRQKpnbrqk'
const PROMOTIONS: PromotionPiece[] = ['q', 'r', 'b', 'n']

// [row delta, col delta]; row 0 is rank 8, so "up the board" is -1.
const KNIGHT_STEPS: [number, number][] = [
  [-2, -1],
  [-2, 1],
  [-1, -2],
  [-1, 2],
  [1, -2],
  [1, 2],
  [2, -1],
  [2, 1],
]

const KING_STEPS: [number, number][] = [
  [-1, -1],
  [-1, 0],
  [-1, 1],
  [0, -1],
  [0, 1],
  [1, -1],
  [1, 0],
  [1, 1],
]

const ROOK_RAYS: [number, number][] = [
  [-1, 0],
  [1, 0],
  [0, -1],
  [0, 1],
]

const BISHOP_RAYS: [number, number][] = [
  [-1, -1],
  [-1, 1],
  [1, -1],
  [1, 1],
]

const WHITE_KING_HOME = 60
const BLACK_KING_HOME = 4

function rowOf(square: number): number {
  return square >> 3
}

function colOf(square: number): number {
  return square & 7
}

function inBounds(row: number, col: number): boolean {
  return row >= 0 && row < 8 && col >= 0 && col < 8
}

/**
 * The color of the piece in a cell.
 *
 * @param cell A board cell, a piece letter or the empty string.
 * @returns 'w' or 'b', or null for an empty cell.
 */
export function colorOf(cell: Cell): Color | null {
  if (cell === '') return null
  return cell === cell.toUpperCase() ? 'w' : 'b'
}

/**
 * The other color.
 *
 * @param color A color.
 * @returns The opposite color.
 */
export function opposite(color: Color): Color {
  return color === 'w' ? 'b' : 'w'
}

/**
 * The name of a square.
 *
 * @param square Square index, 0 for a8 up to 63 for h1.
 * @returns The name, such as "e4" for index 36.
 */
export function squareName(square: number): string {
  return FILES[colOf(square)] + String(8 - rowOf(square))
}

/**
 * The index of a square.
 *
 * @param name A square name such as "e4".
 * @returns The square index.
 * @throws When the text is not a square name.
 */
export function squareIndex(name: string): number {
  const col = FILES.indexOf(name[0])
  const rank = Number(name[1])
  if (name.length !== 2 || col < 0 || !Number.isInteger(rank) || rank < 1 || rank > 8) {
    throw new Error(`Invalid square: ${name}`)
  }
  return (8 - rank) * 8 + col
}

function emptyCastling(): CastlingRights {
  return { K: false, Q: false, k: false, q: false }
}

/**
 * Parses a FEN string. Trailing fields may be missing; only the piece placement is required.
 *
 * @param fen The FEN string.
 * @returns The position. A missing side to move is white, missing castling rights and en passant
 *   square are none, and the counters are 0 and 1.
 */
export function parseFen(fen: string): Position {
  const fields = fen.trim().split(/\s+/)
  const ranks = fields[0].split('/')
  if (ranks.length !== 8) throw new Error(`Invalid FEN, expected 8 ranks: ${fen}`)

  const board: Board = new Array<Cell>(64).fill('')
  ranks.forEach((rank, row) => {
    let col = 0
    for (const ch of rank) {
      if (ch >= '1' && ch <= '8') {
        col += Number(ch)
      } else if (PIECES.includes(ch)) {
        if (col > 7) throw new Error(`Invalid FEN, rank too long: ${fen}`)
        board[row * 8 + col] = ch as Piece
        col++
      } else {
        throw new Error(`Invalid FEN, unknown piece ${ch}: ${fen}`)
      }
    }
    if (col !== 8) throw new Error(`Invalid FEN, rank ${8 - row} does not have 8 files: ${fen}`)
  })

  for (const king of ['K', 'k']) {
    if (board.filter((c) => c === king).length !== 1) {
      throw new Error(`Invalid FEN, expected exactly one ${king}: ${fen}`)
    }
  }

  const turnField = fields[1] ?? 'w'
  if (turnField !== 'w' && turnField !== 'b') throw new Error(`Invalid FEN side to move: ${fen}`)

  const castling = emptyCastling()
  for (const ch of fields[2] ?? '-') {
    if (ch === '-') continue
    if (!(ch in castling)) throw new Error(`Invalid FEN castling field: ${fen}`)
    castling[ch as keyof CastlingRights] = true
  }

  const epField = fields[3] ?? '-'
  const enPassant = epField === '-' ? null : squareIndex(epField)

  const halfmove = Number(fields[4] ?? 0)
  const fullmove = Number(fields[5] ?? 1)
  if (!Number.isInteger(halfmove) || halfmove < 0 || !Number.isInteger(fullmove) || fullmove < 1) {
    throw new Error(`Invalid FEN move counters: ${fen}`)
  }

  return { board, turn: turnField, castling, enPassant, halfmove, fullmove }
}

/**
 * The FEN string of a position.
 *
 * @param position The position.
 * @returns The FEN string.
 */
export function toFen(position: Position): string {
  const ranks: string[] = []
  for (let row = 0; row < 8; row++) {
    let rank = ''
    let empty = 0
    for (let col = 0; col < 8; col++) {
      const cell = position.board[row * 8 + col]
      if (cell === '') {
        empty++
      } else {
        if (empty > 0) rank += String(empty)
        empty = 0
        rank += cell
      }
    }
    if (empty > 0) rank += String(empty)
    ranks.push(rank)
  }

  const { K, Q, k, q } = position.castling
  const castling = (K ? 'K' : '') + (Q ? 'Q' : '') + (k ? 'k' : '') + (q ? 'q' : '') || '-'
  const ep = position.enPassant === null ? '-' : squareName(position.enPassant)
  const counters = `${position.halfmove} ${position.fullmove}`
  return `${ranks.join('/')} ${position.turn} ${castling} ${ep} ${counters}`
}

/**
 * The position at the start of a game.
 *
 * @returns The initial position.
 */
export function createInitialPosition(): Position {
  return parseFen(START_FEN)
}

/**
 * Whether a side attacks a square, ignoring pins.
 *
 * @param board The board.
 * @param square Square index of the target.
 * @param by The attacking color.
 * @returns True when a piece of `by` attacks `square`.
 */
export function isSquareAttacked(board: Board, square: number, by: Color): boolean {
  const row = rowOf(square)
  const col = colOf(square)
  const pawn: Piece = by === 'w' ? 'P' : 'p'
  const knight: Piece = by === 'w' ? 'N' : 'n'
  const king: Piece = by === 'w' ? 'K' : 'k'
  const bishop: Piece = by === 'w' ? 'B' : 'b'
  const rook: Piece = by === 'w' ? 'R' : 'r'
  const queen: Piece = by === 'w' ? 'Q' : 'q'

  // A white pawn attacks up the board (towards row 0), so it sits one row below the target.
  const pawnRow = by === 'w' ? row + 1 : row - 1
  for (const dc of [-1, 1]) {
    if (inBounds(pawnRow, col + dc) && board[pawnRow * 8 + col + dc] === pawn) return true
  }

  for (const [dr, dc] of KNIGHT_STEPS) {
    if (inBounds(row + dr, col + dc) && board[(row + dr) * 8 + col + dc] === knight) return true
  }
  for (const [dr, dc] of KING_STEPS) {
    if (inBounds(row + dr, col + dc) && board[(row + dr) * 8 + col + dc] === king) return true
  }

  const slides = (rays: [number, number][], first: Piece): boolean => {
    for (const [dr, dc] of rays) {
      let r = row + dr
      let c = col + dc
      while (inBounds(r, c)) {
        const cell = board[r * 8 + c]
        if (cell !== '') {
          if (cell === first || cell === queen) return true
          break
        }
        r += dr
        c += dc
      }
    }
    return false
  }
  return slides(ROOK_RAYS, rook) || slides(BISHOP_RAYS, bishop)
}

function findKing(board: Board, color: Color): number {
  return board.indexOf(color === 'w' ? 'K' : 'k')
}

/**
 * Whether a king is attacked.
 *
 * @param position The position.
 * @param color The side whose king is tested; the side to move by default.
 * @returns True when that king is in check.
 */
export function isInCheck(position: Position, color: Color = position.turn): boolean {
  return isSquareAttacked(position.board, findKing(position.board, color), opposite(color))
}

/**
 * Moves that obey piece movement, but may leave the mover's own king attacked.
 *
 * @param position The position.
 * @returns The moves of the side to move.
 */
function pseudoLegalMoves(position: Position): Move[] {
  const { board, turn, castling, enPassant } = position
  const white = turn === 'w'
  const moves: Move[] = []

  const pushPawnMove = (from: number, to: number) => {
    const lastRow = white ? 0 : 7
    if (rowOf(to) === lastRow) {
      for (const promotion of PROMOTIONS) moves.push({ from, to, promotion })
    } else {
      moves.push({ from, to })
    }
  }

  for (let from = 0; from < 64; from++) {
    const piece = board[from]
    if (piece === '' || colorOf(piece) !== turn) continue
    const row = rowOf(from)
    const col = colOf(from)

    switch (piece.toLowerCase()) {
      case 'p': {
        const dir = white ? -1 : 1
        const startRow = white ? 6 : 1
        const oneRow = row + dir
        if (inBounds(oneRow, col) && board[oneRow * 8 + col] === '') {
          pushPawnMove(from, oneRow * 8 + col)
          if (row === startRow && board[(row + 2 * dir) * 8 + col] === '') {
            moves.push({ from, to: (row + 2 * dir) * 8 + col })
          }
        }
        for (const dc of [-1, 1]) {
          if (!inBounds(oneRow, col + dc)) continue
          const to = oneRow * 8 + col + dc
          const target = board[to]
          if ((target !== '' && colorOf(target) !== turn) || (target === '' && to === enPassant)) {
            pushPawnMove(from, to)
          }
        }
        break
      }
      case 'n':
      case 'k': {
        const steps = piece.toLowerCase() === 'n' ? KNIGHT_STEPS : KING_STEPS
        for (const [dr, dc] of steps) {
          if (!inBounds(row + dr, col + dc)) continue
          const to = (row + dr) * 8 + col + dc
          if (colorOf(board[to]) !== turn) moves.push({ from, to })
        }
        break
      }
      default: {
        const kind = piece.toLowerCase()
        const rays =
          kind === 'r' ? ROOK_RAYS : kind === 'b' ? BISHOP_RAYS : [...ROOK_RAYS, ...BISHOP_RAYS]
        for (const [dr, dc] of rays) {
          let r = row + dr
          let c = col + dc
          while (inBounds(r, c)) {
            const to = r * 8 + c
            const target = board[to]
            if (target === '') {
              moves.push({ from, to })
            } else {
              if (colorOf(target) !== turn) moves.push({ from, to })
              break
            }
            r += dr
            c += dc
          }
        }
      }
    }
  }

  // Castling: rights intact, squares between empty, and the king does not start on, cross or land
  // on an attacked square.
  const home = white ? WHITE_KING_HOME : BLACK_KING_HOME
  const king: Piece = white ? 'K' : 'k'
  const rook: Piece = white ? 'R' : 'r'
  const enemy = opposite(turn)
  if (board[home] === king && !isSquareAttacked(board, home, enemy)) {
    const kingSide = white ? castling.K : castling.k
    const queenSide = white ? castling.Q : castling.q
    if (
      kingSide &&
      board[home + 3] === rook &&
      board[home + 1] === '' &&
      board[home + 2] === '' &&
      !isSquareAttacked(board, home + 1, enemy) &&
      !isSquareAttacked(board, home + 2, enemy)
    ) {
      moves.push({ from: home, to: home + 2 })
    }
    if (
      queenSide &&
      board[home - 4] === rook &&
      board[home - 1] === '' &&
      board[home - 2] === '' &&
      board[home - 3] === '' &&
      !isSquareAttacked(board, home - 1, enemy) &&
      !isSquareAttacked(board, home - 2, enemy)
    ) {
      moves.push({ from: home, to: home - 2 })
    }
  }

  return moves
}

/**
 * The board after a move, with en passant, castling and promotion applied.
 *
 * @param board The board before the move.
 * @param move The move.
 * @param enPassant The en passant target square, or null.
 * @returns A new board.
 */
function boardAfter(board: Board, move: Move, enPassant: number | null): Board {
  const next = board.slice()
  const piece = board[move.from]
  const kind = piece.toLowerCase()
  const color = colorOf(piece)

  if (
    kind === 'p' &&
    move.to === enPassant &&
    board[move.to] === '' &&
    colOf(move.from) !== colOf(move.to)
  ) {
    next[rowOf(move.from) * 8 + colOf(move.to)] = ''
  }

  if (kind === 'k' && Math.abs(colOf(move.to) - colOf(move.from)) === 2) {
    const kingSide = colOf(move.to) > colOf(move.from)
    const rookFrom = move.from + (kingSide ? 3 : -4)
    const rookTo = move.from + (kingSide ? 1 : -1)
    next[rookTo] = next[rookFrom]
    next[rookFrom] = ''
  }

  next[move.to] = move.promotion
    ? ((color === 'w' ? move.promotion.toUpperCase() : move.promotion) as Piece)
    : piece
  next[move.from] = ''
  return next
}

/**
 * Moves the side to move may play.
 *
 * @param position The position.
 * @returns The legal moves.
 */
export function getLegalMoves(position: Position): Move[] {
  const { board, turn, enPassant } = position
  return pseudoLegalMoves(position).filter((move) => {
    const after = boardAfter(board, move, enPassant)
    return !isSquareAttacked(after, findKing(after, turn), opposite(turn))
  })
}

/**
 * Whether the side to move has a legal move.
 *
 * @param position The position.
 * @returns True when there is at least one.
 */
export function hasLegalMoves(position: Position): boolean {
  return getLegalMoves(position).length > 0
}

/**
 * Whether a move is a pawn reaching the last rank.
 *
 * @param position The position.
 * @param from Square index the piece moves from.
 * @param to Square index it moves to.
 * @returns True when the move promotes a pawn.
 */
export function isPromotionMove(position: Position, from: number, to: number): boolean {
  const piece = position.board[from]
  if (piece.toLowerCase() !== 'p') return false
  return rowOf(to) === (piece === 'P' ? 0 : 7)
}

/**
 * Applies a move. The move is assumed legal. A pawn reaching the last rank without a `promotion`
 * becomes a queen.
 *
 * @param position The position before the move.
 * @param move The move.
 * @returns A new position; `position` is not changed.
 */
export function applyMove(position: Position, move: Move): Position {
  const { board, turn, castling, enPassant } = position
  const piece = board[move.from]
  const isPawn = piece.toLowerCase() === 'p'
  const isCapture =
    board[move.to] !== '' ||
    (isPawn && move.to === enPassant && colOf(move.from) !== colOf(move.to))

  const effective: Move =
    isPawn && isPromotionMove(position, move.from, move.to) && !move.promotion
      ? { ...move, promotion: 'q' }
      : move

  const rights = { ...castling }
  if (piece === 'K') {
    rights.K = false
    rights.Q = false
  } else if (piece === 'k') {
    rights.k = false
    rights.q = false
  }
  for (const square of [move.from, move.to]) {
    if (square === 0) rights.q = false
    else if (square === 7) rights.k = false
    else if (square === 56) rights.Q = false
    else if (square === 63) rights.K = false
  }

  const doublePush = isPawn && Math.abs(rowOf(move.to) - rowOf(move.from)) === 2
  return {
    board: boardAfter(board, effective, enPassant),
    turn: opposite(turn),
    castling: rights,
    enPassant: doublePush ? (move.from + move.to) / 2 : null,
    halfmove: isPawn || isCapture ? 0 : position.halfmove + 1,
    fullmove: position.fullmove + (turn === 'b' ? 1 : 0),
  }
}

/**
 * Counts the leaf positions reachable in exactly `depth` plies.
 *
 * @param position The position to start from.
 * @param depth Number of plies.
 * @returns The number of leaves.
 */
export function perft(position: Position, depth: number): number {
  if (depth === 0) return 1
  const moves = getLegalMoves(position)
  if (depth === 1) return moves.length
  let nodes = 0
  for (const move of moves) nodes += perft(applyMove(position, move), depth - 1)
  return nodes
}

/**
 * A move in UCI notation.
 *
 * @param move The move.
 * @returns The text, such as "e2e4" or "e7e8q".
 */
export function moveToUci(move: Move): string {
  return squareName(move.from) + squareName(move.to) + (move.promotion ?? '')
}

/**
 * A move from UCI notation.
 *
 * @param uci The text, such as "e2e4" or "e7e8q".
 * @returns The move.
 */
export function uciToMove(uci: string): Move {
  if (uci.length < 4 || uci.length > 5) throw new Error(`Invalid move: ${uci}`)
  const move: Move = { from: squareIndex(uci.slice(0, 2)), to: squareIndex(uci.slice(2, 4)) }
  if (uci.length === 5) {
    const promotion = uci[4] as PromotionPiece
    if (!PROMOTIONS.includes(promotion)) throw new Error(`Invalid promotion: ${uci}`)
    move.promotion = promotion
  }
  return move
}

/**
 * The legal move that equals a candidate. A promotion without a piece is a queen.
 *
 * @param position The position.
 * @param candidate The move to look for.
 * @returns The legal move, or null when there is none.
 */
export function findLegalMove(position: Position, candidate: Move): Move | null {
  const wanted = isPromotionMove(position, candidate.from, candidate.to)
    ? { ...candidate, promotion: candidate.promotion ?? 'q' }
    : candidate
  return (
    getLegalMoves(position).find(
      (m) => m.from === wanted.from && m.to === wanted.to && m.promotion === wanted.promotion,
    ) ?? null
  )
}

/**
 * Standard algebraic notation for a legal move, with disambiguation, capture, promotion and check
 * or mate marks.
 *
 * @param position The position before the move.
 * @param move A legal move.
 * @returns The notation, such as "Nbd7+".
 */
export function toSan(position: Position, move: Move): string {
  const piece = position.board[move.from]
  const kind = piece.toLowerCase()
  const isEnPassant =
    kind === 'p' && move.to === position.enPassant && colOf(move.from) !== colOf(move.to)
  const isCapture = position.board[move.to] !== '' || isEnPassant

  let san: string
  if (kind === 'k' && Math.abs(colOf(move.to) - colOf(move.from)) === 2) {
    san = colOf(move.to) > colOf(move.from) ? 'O-O' : 'O-O-O'
  } else if (kind === 'p') {
    san = isCapture ? FILES[colOf(move.from)] + 'x' : ''
    san += squareName(move.to)
    if (move.promotion) san += '=' + move.promotion.toUpperCase()
  } else {
    san = piece.toUpperCase()
    const rivals = getLegalMoves(position).filter(
      (m) => m.to === move.to && m.from !== move.from && position.board[m.from] === piece,
    )
    if (rivals.length > 0) {
      const sameFile = rivals.some((m) => colOf(m.from) === colOf(move.from))
      const sameRank = rivals.some((m) => rowOf(m.from) === rowOf(move.from))
      if (!sameFile) san += FILES[colOf(move.from)]
      else if (!sameRank) san += String(8 - rowOf(move.from))
      else san += squareName(move.from)
    }
    if (isCapture) san += 'x'
    san += squareName(move.to)
  }

  const after = applyMove(position, move)
  if (isInCheck(after)) san += hasLegalMoves(after) ? '+' : '#'
  return san
}

/**
 * A key that identifies a position for repetition: placement, side to move, castling rights, and
 * the en passant square only when a pawn could take.
 *
 * @param position The position.
 * @returns The key.
 */
export function positionKey(position: Position): string {
  const [placement] = toFen(position).split(' ')
  const { castling, turn, enPassant } = position
  const rights =
    (castling.K ? 'K' : '') +
    (castling.Q ? 'Q' : '') +
    (castling.k ? 'k' : '') +
    (castling.q ? 'q' : '')

  let ep = '-'
  if (enPassant !== null) {
    const pawn: Piece = turn === 'w' ? 'P' : 'p'
    const pawnRow = turn === 'w' ? rowOf(enPassant) + 1 : rowOf(enPassant) - 1
    const capturable = [-1, 1].some(
      (dc) =>
        inBounds(pawnRow, colOf(enPassant) + dc) &&
        position.board[pawnRow * 8 + colOf(enPassant) + dc] === pawn,
    )
    if (capturable) ep = squareName(enPassant)
  }
  return `${placement} ${turn} ${rights || '-'} ${ep}`
}

/**
 * Whether neither side can deliver mate: bare kings, one minor piece, or same-colored bishops
 * only.
 *
 * @param position The position.
 * @returns True when the material is insufficient.
 */
export function hasInsufficientMaterial(position: Position): boolean {
  const minors: { piece: Piece; square: number }[] = []
  for (let square = 0; square < 64; square++) {
    const cell = position.board[square]
    if (cell === '' || cell.toLowerCase() === 'k') continue
    const kind = cell.toLowerCase()
    if (kind !== 'n' && kind !== 'b') return false
    minors.push({ piece: cell, square })
  }
  if (minors.length <= 1) return true
  if (minors.every((m) => m.piece.toLowerCase() === 'b')) {
    const shade = (square: number) => (rowOf(square) + colOf(square)) % 2
    return minors.every((m) => shade(m.square) === shade(minors[0].square))
  }
  return false
}

function evaluateStatus(
  position: Position,
  keys: string[],
): { status: GameStatus; drawReason: DrawReason | null; winner: Color | null; inCheck: boolean } {
  const inCheck = isInCheck(position)
  if (!hasLegalMoves(position)) {
    return inCheck
      ? { status: 'checkmate', drawReason: null, winner: opposite(position.turn), inCheck }
      : { status: 'stalemate', drawReason: null, winner: null, inCheck }
  }

  const key = keys[keys.length - 1]
  const draw = (drawReason: DrawReason) => ({
    status: 'draw' as const,
    drawReason,
    winner: null,
    inCheck,
  })
  if (hasInsufficientMaterial(position)) return draw('insufficient-material')
  if (position.halfmove >= 100) return draw('fifty-move')
  if (keys.filter((k) => k === key).length >= 3) return draw('repetition')
  return { status: 'playing', drawReason: null, winner: null, inCheck }
}

/**
 * The state at the start of a game.
 *
 * @param fen The starting position; the standard one by default.
 * @returns A state with no moves played.
 */
export function createInitialState(fen: string = START_FEN): GameState {
  const position = parseFen(fen)
  const keys = [positionKey(position)]
  return {
    position,
    startFen: fen,
    moves: [],
    sans: [],
    keys,
    ...evaluateStatus(position, keys),
    lastMove: null,
  }
}

/**
 * Applies a move and recomputes the status of the game. The move is assumed legal. Does nothing
 * once the game is over.
 *
 * @param state The state before the move.
 * @param move The move.
 * @returns The next state.
 */
export function advanceTurn(state: GameState, move: Move): GameState {
  if (state.status !== 'playing') return state

  const san = toSan(state.position, move)
  const position = applyMove(state.position, move)
  const keys = [...state.keys, positionKey(position)]
  const applied: Move =
    move.promotion || !isPromotionMove(state.position, move.from, move.to)
      ? move
      : { ...move, promotion: 'q' }

  return {
    position,
    startFen: state.startFen,
    moves: [...state.moves, applied],
    sans: [...state.sans, san],
    keys,
    ...evaluateStatus(position, keys),
    lastMove: applied,
  }
}

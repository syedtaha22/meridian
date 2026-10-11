import { describe, expect, it } from 'vitest'
import {
  START_FEN,
  advanceTurn,
  applyMove,
  createInitialPosition,
  createInitialState,
  findLegalMove,
  getLegalMoves,
  hasInsufficientMaterial,
  isInCheck,
  moveToUci,
  parseFen,
  perft,
  positionKey,
  squareIndex,
  squareName,
  toFen,
  toSan,
  uciToMove,
} from '../src/lib/chess-rules'
import type { GameState } from '../src/lib/types'

/**
 * Plays UCI moves from a state, asserting every one of them is legal.
 */
function play(state: GameState, ...ucis: string[]): GameState {
  let next = state
  for (const uci of ucis) {
    const move = findLegalMove(next.position, uciToMove(uci))
    expect(move, `${uci} should be legal`).not.toBeNull()
    next = advanceTurn(next, move!)
  }
  return next
}

describe('FEN', () => {
  it('round-trips the starting position', () => {
    expect(toFen(createInitialPosition())).toBe(START_FEN)
  })

  it('round-trips positions with castling, en passant and counters', () => {
    const fen = 'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b Kq e3 7 23'
    expect(toFen(parseFen(fen))).toBe(fen)
  })

  it('accepts the short FENs the benchmark suite uses', () => {
    const position = parseFen('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq')
    expect(position.enPassant).toBeNull()
    expect(position.halfmove).toBe(0)
    expect(position.fullmove).toBe(1)
  })

  it('rejects malformed input', () => {
    expect(() => parseFen('8/8/8 w - - 0 1')).toThrow()
    expect(() => parseFen('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN w KQkq - 0 1')).toThrow()
    expect(() => parseFen('8/8/8/8/8/8/8/8 w - - 0 1')).toThrow()
  })
})

describe('squares and UCI', () => {
  it('matches the backend index convention (a8 = 0, h1 = 63)', () => {
    expect(squareIndex('a8')).toBe(0)
    expect(squareIndex('h8')).toBe(7)
    expect(squareIndex('a1')).toBe(56)
    expect(squareIndex('e1')).toBe(60)
    expect(squareName(63)).toBe('h1')
  })

  it('converts moves, including promotions', () => {
    expect(moveToUci(uciToMove('e2e4'))).toBe('e2e4')
    expect(uciToMove('e7e8q')).toEqual({ from: 12, to: 4, promotion: 'q' })
    expect(() => uciToMove('e7e8k')).toThrow()
  })
})

// Node counts from https://www.chessprogramming.org/Perft_Results
const PERFT_CASES: { name: string; fen: string; counts: number[] }[] = [
  { name: 'initial position', fen: START_FEN, counts: [20, 400, 8902, 197281] },
  {
    name: 'kiwipete',
    fen: 'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1',
    counts: [48, 2039, 97862],
  },
  {
    name: 'position 3',
    fen: '8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1',
    counts: [14, 191, 2812, 43238],
  },
  {
    name: 'position 4',
    fen: 'r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1',
    counts: [6, 264, 9467],
  },
  {
    name: 'position 5',
    fen: 'rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8',
    counts: [44, 1486, 62379],
  },
  {
    name: 'position 6',
    fen: 'r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10',
    counts: [46, 2079, 89890],
  },
]

describe('perft', () => {
  for (const { name, fen, counts } of PERFT_CASES) {
    it(`counts the reference nodes for ${name}`, { timeout: 60_000 }, () => {
      const position = parseFen(fen)
      counts.forEach((expected, i) => {
        expect(perft(position, i + 1), `depth ${i + 1}`).toBe(expected)
      })
    })
  }
})

describe('special moves', () => {
  it('castles king side and moves the rook', () => {
    const position = parseFen('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1')
    const after = applyMove(position, uciToMove('e1g1'))
    expect(toFen(after)).toBe('r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1')
  })

  it('forbids castling through an attacked square', () => {
    const position = parseFen('4k3/8/8/8/8/8/5r2/R3K2R w KQ - 0 1')
    const ucis = getLegalMoves(position).map(moveToUci)
    expect(ucis).not.toContain('e1g1')
    expect(ucis).toContain('e1c1')
  })

  it('forbids castling out of check', () => {
    const position = parseFen('4k3/8/8/8/8/8/4r3/R3K2R w KQ - 0 1')
    expect(isInCheck(position)).toBe(true)
    const ucis = getLegalMoves(position).map(moveToUci)
    expect(ucis).not.toContain('e1g1')
    expect(ucis).not.toContain('e1c1')
  })

  it('removes the right to castle when a rook is captured on its corner', () => {
    const position = parseFen('4k3/8/8/8/8/8/6b1/R3K2R b KQ - 0 1')
    const after = applyMove(position, uciToMove('g2h1'))
    expect(after.castling.K).toBe(false)
    expect(after.castling.Q).toBe(true)
  })

  it('plays en passant and removes the captured pawn', () => {
    const position = parseFen('4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2')
    const after = applyMove(position, uciToMove('e5d6'))
    expect(toFen(after)).toBe('4k3/8/3P4/8/8/8/8/4K3 b - - 0 2')
  })

  it('sets the en passant square after a double push only', () => {
    const start = createInitialPosition()
    expect(applyMove(start, uciToMove('e2e4')).enPassant).toBe(squareIndex('e3'))
    expect(applyMove(start, uciToMove('e2e3')).enPassant).toBeNull()
  })

  it('offers all four promotions and defaults to a queen', () => {
    const position = parseFen('8/P6k/8/8/8/8/8/K7 w - - 0 1')
    const promotions = getLegalMoves(position)
      .filter((m) => m.from === squareIndex('a7'))
      .map(moveToUci)
    expect(promotions.sort()).toEqual(['a7a8b', 'a7a8n', 'a7a8q', 'a7a8r'])
    expect(applyMove(position, { from: squareIndex('a7'), to: squareIndex('a8') }).board[0]).toBe(
      'Q',
    )
  })

  it('does not let a pinned piece leave the pin line', () => {
    const position = parseFen('4k3/8/8/8/8/8/4r3/4K1R1 w - - 0 1')
    const rookMoves = getLegalMoves(position).filter((m) => m.from === squareIndex('g1'))
    expect(rookMoves).toEqual([])
  })
})

describe('notation', () => {
  it('writes standard algebraic notation with marks', () => {
    const fools = play(createInitialState(), 'f2f3', 'e7e5', 'g2g4', 'd8h4')
    expect(fools.sans).toEqual(['f3', 'e5', 'g4', 'Qh4#'])
  })

  it('disambiguates by file, rank or both', () => {
    const byFile = parseFen('4k3/8/8/8/8/8/8/N1N1K3 w - - 0 1')
    expect(toSan(byFile, uciToMove('a1b3'))).toBe('Nab3')
    const byRank = parseFen('4k3/8/8/N7/8/8/8/N3K3 w - - 0 1')
    expect(toSan(byRank, uciToMove('a1b3'))).toBe('N1b3')
    const both = parseFen('7k/8/8/1Q1Q4/8/8/8/3QK3 w - - 0 1')
    expect(toSan(both, uciToMove('d5b3'))).toBe('Qd5b3')
  })

  it('writes castling, captures and promotions', () => {
    const castle = parseFen('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1')
    expect(toSan(castle, uciToMove('e1g1'))).toBe('O-O')
    expect(toSan(castle, uciToMove('e1c1'))).toBe('O-O-O')
    const promo = parseFen('1n6/P7/7k/8/8/8/8/K7 w - - 0 1')
    expect(toSan(promo, uciToMove('a7a8q'))).toBe('a8=Q')
    expect(toSan(promo, uciToMove('a7b8q'))).toBe('axb8=Q')
  })
})

describe('game status', () => {
  it('detects checkmate and names the winner', () => {
    const state = play(createInitialState(), 'f2f3', 'e7e5', 'g2g4', 'd8h4')
    expect(state.status).toBe('checkmate')
    expect(state.winner).toBe('b')
    expect(state.inCheck).toBe(true)
  })

  it('detects stalemate', () => {
    const state = createInitialState('7k/5Q2/6K1/8/8/8/8/8 b - - 0 1')
    expect(state.status).toBe('stalemate')
    expect(state.winner).toBeNull()
  })

  it('detects threefold repetition', () => {
    const shuffle = ['g1f3', 'g8f6', 'f3g1', 'f6g8']
    const state = play(createInitialState(), ...shuffle, ...shuffle)
    expect(state.status).toBe('draw')
    expect(state.drawReason).toBe('repetition')
  })

  it('detects the fifty-move rule', () => {
    const state = createInitialState('4k3/8/8/8/8/8/R7/4K3 w - - 99 80')
    expect(state.status).toBe('playing')
    expect(play(state, 'a2a3').drawReason).toBe('fifty-move')
  })

  it('detects insufficient material', () => {
    expect(hasInsufficientMaterial(parseFen('4k3/8/8/8/8/8/8/4K3 w - - 0 1'))).toBe(true)
    expect(hasInsufficientMaterial(parseFen('4k3/8/8/8/8/8/8/3NK3 w - - 0 1'))).toBe(true)
    expect(hasInsufficientMaterial(parseFen('4kb2/8/8/8/8/8/8/2B1K3 w - - 0 1'))).toBe(true)
    expect(hasInsufficientMaterial(parseFen('4kb2/8/8/8/8/8/8/4KB2 w - - 0 1'))).toBe(false)
    expect(hasInsufficientMaterial(parseFen('4k3/8/8/8/8/8/P7/4K3 w - - 0 1'))).toBe(false)
    expect(hasInsufficientMaterial(parseFen('4k3/8/8/8/8/8/8/3RK3 w - - 0 1'))).toBe(false)
  })

  it('does not count an en passant square nobody can use as a different position', () => {
    const withEp = parseFen('4k3/8/8/8/4P3/8/8/4K3 b - e3 0 1')
    const without = parseFen('4k3/8/8/8/4P3/8/8/4K3 b - - 0 1')
    expect(positionKey(withEp)).toBe(positionKey(without))
  })

  it('is a no-op once the game is over', () => {
    const over = play(createInitialState(), 'f2f3', 'e7e5', 'g2g4', 'd8h4')
    expect(advanceTurn(over, uciToMove('a2a3'))).toBe(over)
  })
})

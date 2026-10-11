import { colorOf } from '../../lib/chess-rules'
import type { Color, GameState } from '../../lib/types'

interface GameStatusBarProps {
  state: GameState
  humanColor: Color
  aiThinking: boolean
  error: string | null
}

const VALUES: Record<string, number> = { p: 1, n: 3, b: 3, r: 5, q: 9 }

function material(state: GameState, color: Color): number {
  return state.position.board.reduce((sum, cell) => {
    if (cell === '' || colorOf(cell) !== color) return sum
    return sum + (VALUES[cell.toLowerCase()] ?? 0)
  }, 0)
}

function MaterialBadge({
  color,
  points,
  label,
  inCheck,
}: {
  color: string
  points: number
  label: string
  inCheck: boolean
}) {
  return (
    <div
      className={`chess-badge${inCheck ? ' is-check' : ''}`}
      aria-label={`${label}: ${points} points of material${inCheck ? ', in check' : ''}`}
    >
      <span className="chess-badge-dot" style={{ background: color }} aria-hidden="true" />
      <span>{points}</span>
    </div>
  )
}

const DRAW_REASONS = {
  'fifty-move': 'fifty-move rule',
  repetition: 'threefold repetition',
  'insufficient-material': 'insufficient material',
} as const

export function GameStatusBar({ state, humanColor, aiThinking, error }: GameStatusBarProps) {
  let status: string
  if (error) {
    status = `Error: ${error}`
  } else if (state.status === 'checkmate') {
    status = `Checkmate. ${state.winner === 'w' ? 'White' : 'Black'} wins.`
  } else if (state.status === 'stalemate') {
    status = 'Stalemate. The game is a draw.'
  } else if (state.status === 'draw') {
    status = `Draw by ${state.drawReason ? DRAW_REASONS[state.drawReason] : 'agreement'}.`
  } else if (aiThinking) {
    status = 'Engine is thinking...'
  } else if (state.position.turn === humanColor) {
    status = state.inCheck ? 'Your move. You are in check.' : 'Your move.'
  } else {
    status = "Engine's move."
  }

  return (
    <div className="chess-status">
      <MaterialBadge
        color="#4a4646"
        points={material(state, 'b')}
        label="Black"
        inCheck={state.inCheck && state.position.turn === 'b'}
      />
      <span className="chess-status-text">{status}</span>
      <MaterialBadge
        color="#f5f5f4"
        points={material(state, 'w')}
        label="White"
        inCheck={state.inCheck && state.position.turn === 'w'}
      />
    </div>
  )
}

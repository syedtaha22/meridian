import type { MouseEvent } from 'react'
import type { Cell } from '../../lib/types'
import { PieceIcon } from './PieceIcon'

interface SquareProps {
  piece: Cell

  /**
   * Algebraic name, used for the accessible label.
   */
  name: string
  isLight: boolean

  /**
   * Origin or destination of the last move.
   */
  isLastMove: boolean

  /**
   * "Show move hints": the selected piece can legally move here.
   */
  isLegalTarget: boolean

  /**
   * The hint arrow starts here: its piece is drawn above the arrow, every other piece below.
   */
  isHintSource: boolean

  /**
   * A piece of this square is being dragged, so it is not drawn here.
   */
  isDragSource: boolean

  /**
   * The dragged piece is over this square.
   */
  isDropTarget: boolean

  /**
   * The piece on this square is the one picked up.
   */
  isSelected: boolean

  /**
   * The piece on this square belongs to the player to move and has a legal move.
   */
  isMovable: boolean

  /**
   * Rank digit (top left) and file letter (bottom right) drawn in the corners, each or neither.
   */
  coordinates: { rank: string | null; file: string | null }

  /**
   * Receives keyboard activation only; pointer input is handled by the board.
   */
  onClick: (event: MouseEvent<HTMLButtonElement>) => void
}

// The tiles come from the stylesheet. The highlights are yellow for the last move and red for
// the squares the picked-up piece can reach.
const LIGHT_SQUARE = 'var(--chess-light)'
const DARK_SQUARE = 'var(--chess-dark)'
const LAST_MOVE = 'rgba(255, 255, 0, 0.25)'
const LEGAL_MOVE = 'rgba(255, 0, 0, 0.25)'

const KINDS: Record<string, string> = {
  p: 'pawn',
  n: 'knight',
  b: 'bishop',
  r: 'rook',
  q: 'queen',
  k: 'king',
}

function label(piece: Cell, name: string, isLegalTarget: boolean): string {
  if (piece === '') return isLegalTarget ? `${name}, legal move` : name
  const color = piece === piece.toUpperCase() ? 'White' : 'Black'
  return `${name}, ${color} ${KINDS[piece.toLowerCase()]}`
}

const coordinateStyle = {
  position: 'absolute',
  fontSize: 10,
  fontWeight: 600,
  lineHeight: 1,
} as const

function overlay(color: string): string {
  return `linear-gradient(${color}, ${color})`
}

export function Square({
  piece,
  name,
  isLight,
  isLastMove,
  isLegalTarget,
  isHintSource,
  isDragSource,
  isDropTarget,
  isSelected,
  isMovable,
  coordinates,
  onClick,
}: SquareProps) {
  // Drawn top to bottom: the reachable squares over the last move.
  const layers: string[] = []
  if (isLegalTarget) layers.push(overlay(LEGAL_MOVE))
  if (isLastMove) layers.push(overlay(LAST_MOVE))

  const textColor = isLight ? 'var(--chess-coordinate-on-light)' : 'var(--chess-coordinate-on-dark)'

  return (
    <button
      type="button"
      className="chess-square"
      onClick={onClick}
      aria-label={label(piece, name, isLegalTarget)}
      aria-pressed={isSelected}
      // Some browser extensions rewrite attributes before hydration, which React reports
      // as a mismatch it can't resolve.
      suppressHydrationWarning
      style={{
        position: 'relative',
        width: '100%',
        aspectRatio: '1',
        backgroundColor: isLight ? LIGHT_SQUARE : DARK_SQUARE,
        backgroundImage: layers.length > 0 ? layers.join(', ') : undefined,
        border: 'none',
        padding: 0,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        cursor: isMovable ? 'grab' : 'inherit',
        boxShadow: isDropTarget ? 'inset 0 0 0 4px rgba(255, 255, 255, 0.75)' : undefined,
      }}
    >
      {coordinates.rank !== null && (
        <span aria-hidden="true" style={{ ...coordinateStyle, top: 2, left: 3, color: textColor }}>
          {coordinates.rank}
        </span>
      )}
      {coordinates.file !== null && (
        <span
          aria-hidden="true"
          style={{ ...coordinateStyle, bottom: 1, right: 3, color: textColor }}
        >
          {coordinates.file}
        </span>
      )}

      {piece !== '' && (
        <span
          style={{
            // Above the tile highlights. The hint arrow is at 5: it passes over every piece
            // except the one it is for, which is drawn above it.
            position: 'relative',
            zIndex: isHintSource ? 7 : 1,
            width: '94%',
            height: '94%',
            display: 'block',
            opacity: isDragSource ? 0 : 1,
            pointerEvents: 'none',
          }}
        >
          <PieceIcon piece={piece} />
        </span>
      )}
    </button>
  )
}

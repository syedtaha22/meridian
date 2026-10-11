import type { Piece } from '../../lib/types'

interface PieceIconProps {
  piece: Piece
}

/**
 * One piece, referring to a drawing defined by the piece sprite, which should be rendered once
 * on the page.
 */
export function PieceIcon({ piece }: PieceIconProps) {
  const color = piece === piece.toUpperCase() ? 'w' : 'b'

  return (
    <svg viewBox="0 0 75 75" width="100%" height="100%" aria-hidden="true" focusable="false">
      <use href={`#chess-piece-${color}${piece.toLowerCase()}`} />
    </svg>
  )
}

import type { Color, Piece, PromotionPiece } from '../../lib/types'
import { PieceIcon } from './PieceIcon'

interface PromotionPickerProps {
  color: Color
  onPick: (piece: PromotionPiece) => void
  onCancel: () => void
}

const CHOICES: { piece: PromotionPiece; name: string }[] = [
  { piece: 'q', name: 'queen' },
  { piece: 'r', name: 'rook' },
  { piece: 'b', name: 'bishop' },
  { piece: 'n', name: 'knight' },
]

/**
 * The four pieces a pawn can become, shown under the board when a promotion move is chosen.
 */
export function PromotionPicker({ color, onPick, onCancel }: PromotionPickerProps) {
  return (
    <div className="chess-promotion" role="dialog" aria-label="Choose a promotion piece">
      {CHOICES.map(({ piece, name }) => (
        <button
          key={piece}
          type="button"
          aria-label={`Promote to ${name}`}
          onClick={() => onPick(piece)}
        >
          <PieceIcon piece={(color === 'w' ? piece.toUpperCase() : piece) as Piece} />
        </button>
      ))}
      <button type="button" className="chess-promotion-cancel" onClick={onCancel}>
        Cancel
      </button>
    </div>
  )
}

import { PIECE_SYMBOLS } from '../../lib/piece-sprite'
import type { PieceKey } from '../../lib/piece-sprite'

/**
 * The twelve piece drawings, defined once per page as SVG symbols that the squares refer to. It
 * has zero size and is not hidden, because browsers do not draw the filters of a symbol that
 * is not rendered.
 */
export function PieceSprite() {
  return (
    <svg
      width="0"
      height="0"
      aria-hidden="true"
      focusable="false"
      style={{ position: 'absolute', overflow: 'hidden' }}
    >
      {(Object.keys(PIECE_SYMBOLS) as PieceKey[]).map((key) => (
        <symbol
          key={key}
          id={`chess-piece-${key}`}
          viewBox="0 0 75 75"
          dangerouslySetInnerHTML={{ __html: PIECE_SYMBOLS[key] }}
        />
      ))}
    </svg>
  )
}

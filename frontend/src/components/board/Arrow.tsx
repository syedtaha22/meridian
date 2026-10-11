import type { Color } from '../../lib/types'

interface ArrowProps {
  /**
   * Square index, 0 for a8 across and down to 63 for h1.
   */
  from: number
  to: number

  /**
   * The side at the bottom of the board.
   */
  orientation: Color
}

/**
 * The center of a square in board units (the board is 8 by 8), as it is drawn.
 *
 * @param square Square index, 0 for a8 up to 63 for h1.
 * @param orientation The side at the bottom of the board.
 * @returns The x and y of the center.
 */
function center(square: number, orientation: Color): [number, number] {
  const row = square >> 3
  const col = square & 7
  return orientation === 'w' ? [col + 0.5, row + 0.5] : [7 - col + 0.5, 7 - row + 0.5]
}

const SHAFT_WIDTH = 0.18
const HEAD_LENGTH = 0.38
const HEAD_HALF_WIDTH = 0.28

/**
 * The outline of an arrow between two squares, in board units. A knight move bends: the long
 * side first, then the short one.
 *
 * @param from Square index the arrow starts on.
 * @param to Square index the arrow points at.
 * @param orientation The side at the bottom of the board.
 * @returns The corners of the outline, in order.
 */
export function arrowOutline(from: number, to: number, orientation: Color): [number, number][] {
  const [x1, y1] = center(from, orientation)
  const [x2, y2] = center(to, orientation)
  const dx = x2 - x1
  const dy = y2 - y1
  const knight =
    (Math.abs(dx) === 1 && Math.abs(dy) === 2) || (Math.abs(dx) === 2 && Math.abs(dy) === 1)

  const path: [number, number][] = [[x1, y1]]
  if (knight) {
    path.push(Math.abs(dx) > Math.abs(dy) ? [x2, y1] : [x1, y2])
  }
  path.push([x2, y2])

  // Unit direction of each leg, and the point where the head starts on the last one.
  const legs = path.slice(1).map(([x, y], i) => {
    const [px, py] = path[i]
    const length = Math.hypot(x - px, y - py)
    return [(x - px) / length, (y - py) / length] as [number, number]
  })
  const [lastX, lastY] = legs[legs.length - 1]
  const base: [number, number] = [x2 - lastX * HEAD_LENGTH, y2 - lastY * HEAD_LENGTH]
  const spine: [number, number][] = [...path.slice(0, -1), base]

  const half = SHAFT_WIDTH / 2
  const left: [number, number][] = []
  const right: [number, number][] = []
  spine.forEach(([x, y], i) => {
    // Normal of the leg before this point and of the leg after it.
    const before = legs[Math.max(i - 1, 0)]
    const after = legs[Math.min(i, legs.length - 1)]
    const n0: [number, number] = [-before[1], before[0]]
    const n1: [number, number] = [-after[1], after[0]]
    const scale = half / (1 + n0[0] * n1[0] + n0[1] * n1[1])
    const ox = (n0[0] + n1[0]) * scale
    const oy = (n0[1] + n1[1]) * scale
    left.push([x + ox, y + oy])
    right.push([x - ox, y - oy])
  })

  const [nx, ny] = [-lastY, lastX]
  return [
    ...left,
    [base[0] + nx * HEAD_HALF_WIDTH, base[1] + ny * HEAD_HALF_WIDTH],
    [x2, y2],
    [base[0] - nx * HEAD_HALF_WIDTH, base[1] - ny * HEAD_HALF_WIDTH],
    ...right.reverse(),
  ]
}

/**
 * An arrow from one square to another, drawn over the board like the move arrows of an
 * analysis board, in a translucent green.
 */
export function Arrow({ from, to, orientation }: ArrowProps) {
  const points = arrowOutline(from, to, orientation)
    .map(([x, y]) => `${x.toFixed(3)},${y.toFixed(3)}`)
    .join(' ')

  return (
    <svg className="chess-arrow" viewBox="0 0 8 8" aria-hidden="true">
      <polygon points={points} />
    </svg>
  )
}

import { useRef, useState } from 'react'
import type { PointerEvent } from 'react'

interface LineChartProps {
  /**
   * One value per point, oldest first. The x axis is the 1-based index.
   */
  values: number[]

  /**
   * Label of the x axis, such as "Move".
   */
  xLabel: string

  /**
   * Label of the y axis, such as "Response time (ms)".
   */
  yLabel: string

  /**
   * Shown in the tooltip after the value.
   */
  unit: string

  /**
   * Fixed y range; by default it follows the data from zero.
   */
  domain?: [number, number]

  /**
   * Smallest value the top of the axis may take, so an almost empty chart has a sensible scale.
   */
  minimumTop?: number

  /**
   * Draws a line at y = 0.
   */
  zeroLine?: boolean

  /**
   * Formats a value for the axis and the tooltip.
   */
  format?: (value: number) => string
}

const WIDTH = 480
const HEIGHT = 240
// The left and right margins are equal, so that the drawn area is centered in its box and a pair
// of charts is centered under the board.
const MARGIN = { top: 14, right: 60, bottom: 38, left: 60 }

function niceMax(value: number): number {
  if (value <= 0) return 1
  const power = 10 ** Math.floor(Math.log10(value))
  for (const step of [1, 2, 2.5, 5, 10]) {
    if (value <= step * power) return step * power
  }
  return 10 * power
}

/**
 * A line chart in plain SVG, drawn with the host's custom properties. Hovering shows the nearest
 * point and a click pins it. The height is fixed, so the page does not shift when the first
 * point arrives.
 */
export function LineChart({
  values,
  xLabel,
  yLabel,
  unit,
  domain,
  minimumTop = 1,
  zeroLine = false,
  format = (v) => String(Math.round(v)),
}: LineChartProps) {
  const svgRef = useRef<SVGSVGElement>(null)
  const [hover, setHover] = useState<number | null>(null)
  const [pinned, setPinned] = useState<number | null>(null)

  const innerWidth = WIDTH - MARGIN.left - MARGIN.right
  const innerHeight = HEIGHT - MARGIN.top - MARGIN.bottom

  const [low, high] = domain ?? [0, niceMax(Math.max(...values, minimumTop))]
  const span = high - low || 1
  const xOf = (i: number) =>
    MARGIN.left + (values.length <= 1 ? innerWidth / 2 : (i / (values.length - 1)) * innerWidth)
  const yOf = (v: number) => MARGIN.top + (1 - (v - low) / span) * innerHeight

  const ticks = [0, 0.25, 0.5, 0.75, 1].map((t) => low + t * span)
  const path = values
    .map((v, i) => `${i === 0 ? 'M' : 'L'}${xOf(i).toFixed(1)} ${yOf(v).toFixed(1)}`)
    .join('')

  /**
   * Index of the point nearest the pointer, by horizontal distance.
   */
  const nearest = (event: PointerEvent<SVGSVGElement>): number | null => {
    const svg = svgRef.current
    if (svg === null || values.length === 0) return null
    const rect = svg.getBoundingClientRect()
    const x = ((event.clientX - rect.left) / rect.width) * WIDTH
    let best = 0
    for (let i = 1; i < values.length; i++) {
      if (Math.abs(xOf(i) - x) < Math.abs(xOf(best) - x)) best = i
    }
    return best
  }

  const active = hover ?? (pinned !== null && pinned < values.length ? pinned : null)

  let tooltip = null
  if (active !== null) {
    const text = `${xLabel} ${active + 1}: ${format(values[active])} ${unit}`
    const boxWidth = text.length * 6.7 + 18
    const px = xOf(active)
    const py = yOf(values[active])
    const boxX = px + 10 + boxWidth > WIDTH - 4 ? px - 10 - boxWidth : px + 10
    const boxY = Math.max(MARGIN.top, Math.min(py - 14, HEIGHT - MARGIN.bottom - 28))
    tooltip = (
      <g pointerEvents="none">
        <line
          x1={px}
          x2={px}
          y1={MARGIN.top}
          y2={HEIGHT - MARGIN.bottom}
          className="chess-chart-cursor"
        />
        <circle cx={px} cy={py} r={6} className="chess-chart-dot is-active" />
        <rect x={boxX} y={boxY} width={boxWidth} height={28} rx={7} className="chess-chart-tip" />
        <text x={boxX + 9} y={boxY + 18} className="chess-chart-tip-text">
          {text}
        </text>
      </g>
    )
  }

  const summary =
    values.length === 0
      ? `${yLabel} by ${xLabel.toLowerCase()}, no data yet`
      : `${yLabel} by ${xLabel.toLowerCase()}, ${values.length} points, ` +
        `last ${format(values[values.length - 1])} ${unit}`

  return (
    <svg
      ref={svgRef}
      className="chess-chart"
      viewBox={`0 0 ${WIDTH} ${HEIGHT}`}
      role="img"
      aria-label={summary}
      onPointerMove={(e) => setHover(nearest(e))}
      onPointerLeave={() => setHover(null)}
      onClick={() => {
        if (hover !== null) setPinned((prev) => (prev === hover ? null : hover))
      }}
    >
      {ticks.map((t) => (
        <g key={t}>
          <line
            x1={MARGIN.left}
            x2={WIDTH - MARGIN.right}
            y1={yOf(t)}
            y2={yOf(t)}
            className="chess-chart-grid"
          />
          <text x={MARGIN.left - 8} y={yOf(t) + 4} textAnchor="end" className="chess-chart-tick">
            {format(t)}
          </text>
        </g>
      ))}
      {zeroLine && low < 0 && high > 0 && (
        <line
          x1={MARGIN.left}
          x2={WIDTH - MARGIN.right}
          y1={yOf(0)}
          y2={yOf(0)}
          className="chess-chart-zero"
        />
      )}
      <text
        x={MARGIN.left + innerWidth / 2}
        y={HEIGHT - 8}
        textAnchor="middle"
        className="chess-chart-axis"
      >
        {xLabel}
      </text>
      <text
        transform={`rotate(-90 14 ${MARGIN.top + innerHeight / 2})`}
        x={14}
        y={MARGIN.top + innerHeight / 2}
        textAnchor="middle"
        className="chess-chart-axis"
      >
        {yLabel}
      </text>
      {values.length > 1 && <path d={path} className="chess-chart-line" />}
      {values.map((v, i) => (
        <circle key={i} cx={xOf(i)} cy={yOf(v)} r={3.5} className="chess-chart-dot" />
      ))}
      {tooltip}
    </svg>
  )
}

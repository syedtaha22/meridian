import { useRef, useState } from 'react'
import type { PointerEvent } from 'react'

/**
 * A second series, drawn against its own y axis on the right.
 */
export interface SecondarySeries {
  /**
   * One value per point, oldest first, aligned with the values of the chart.
   */
  values: number[]

  /**
   * Label of the right y axis.
   */
  yLabel: string

  /**
   * Name of the series in the legend.
   */
  legend: string

  /**
   * Shown in the tooltip after the value.
   */
  unit: string

  /**
   * Smallest value the top of the right axis may take.
   */
  minimumTop?: number
}

interface LineChartProps {
  /**
   * One value per point, oldest first. The x axis is the 1-based index.
   */
  values: number[]

  /**
   * A second series on a right y axis, such as the round trip next to the search time.
   */
  secondary?: SecondarySeries

  /**
   * Label of the x axis, such as "Move".
   */
  xLabel: string

  /**
   * Label of the y axis, such as "Response time (ms)".
   */
  yLabel: string

  /**
   * Name of the series in the legend, which is drawn when there is a second series.
   */
  legend?: string

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
const MARGIN = { top: 30, right: 60, bottom: 38, left: 60 }

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
  secondary,
  xLabel,
  yLabel,
  legend,
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

  const secondaryValues = secondary?.values ?? []
  const secondaryTop = niceMax(Math.max(...secondaryValues, secondary?.minimumTop ?? 1))
  const ySecondary = (v: number) => MARGIN.top + (1 - v / secondaryTop) * innerHeight
  const hasSecondary = secondary !== undefined && secondaryValues.length > 0

  // The second series has hollow markers, so that the two series differ in shape and not in
  // color. The radius is the one of the filled dots less half the stroke, so the outer size matches.
  const ring = (cx: number, cy: number, r: number, className: string, key?: string) => (
    <circle key={key} cx={cx} cy={cy} r={r} className={className} />
  )

  const fractions = [0, 0.25, 0.5, 0.75, 1]
  const ticks = fractions.map((t) => low + t * span)
  const pathOf = (series: number[], scale: (v: number) => number) =>
    series
      .map((v, i) => `${i === 0 ? 'M' : 'L'}${xOf(i).toFixed(1)} ${scale(v).toFixed(1)}`)
      .join('')
  const path = pathOf(values, yOf)
  const secondaryPath = pathOf(secondaryValues, ySecondary)

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
    const second =
      hasSecondary && active < secondaryValues.length
        ? `, ${format(secondaryValues[active])} ${secondary.unit}`
        : ''
    const text = `${xLabel} ${active + 1}: ${format(values[active])} ${unit}${second}`
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
        {hasSecondary &&
          active < secondaryValues.length &&
          ring(px, ySecondary(secondaryValues[active]), 5, 'chess-chart-ring is-active')}
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
        `last ${format(values[values.length - 1])} ${unit}` +
        (hasSecondary
          ? `, and ${secondary.yLabel}, last ` +
            `${format(secondaryValues[secondaryValues.length - 1])} ${secondary.unit}`
          : '')

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
      {hasSecondary &&
        fractions.map((t) => (
          <text
            key={t}
            x={WIDTH - MARGIN.right + 8}
            y={yOf(low + t * span) + 4}
            className="chess-chart-tick"
          >
            {format(t * secondaryTop)}
          </text>
        ))}
      {hasSecondary && secondary !== undefined && (
        <text
          transform={`rotate(90 ${WIDTH - 14} ${MARGIN.top + innerHeight / 2})`}
          x={WIDTH - 14}
          y={MARGIN.top + innerHeight / 2}
          textAnchor="middle"
          className="chess-chart-axis"
        >
          {secondary.yLabel}
        </text>
      )}
      {values.length > 1 && <path d={path} className="chess-chart-line" />}
      {secondaryValues.length > 1 && (
        <path d={secondaryPath} className="chess-chart-line chess-chart-line-dashed" />
      )}
      {values.map((v, i) => (
        <circle key={i} cx={xOf(i)} cy={yOf(v)} r={3.5} className="chess-chart-dot" />
      ))}
      {secondaryValues.map((v, i) => ring(xOf(i), ySecondary(v), 2.5, 'chess-chart-ring', `s${i}`))}
      {hasSecondary && secondary !== undefined && (
        <g className="chess-chart-legend">
          <circle cx={MARGIN.left + 4} cy={11} r={3.5} className="chess-chart-dot" />
          <text x={MARGIN.left + 14} y={15} className="chess-chart-tick">
            {legend ?? yLabel} (left axis)
          </text>
          {ring(MARGIN.left + 160, 11, 2.5, 'chess-chart-ring')}
          <text x={MARGIN.left + 170} y={15} className="chess-chart-tick">
            {secondary.legend} (right axis)
          </text>
        </g>
      )}
      {tooltip}
    </svg>
  )
}

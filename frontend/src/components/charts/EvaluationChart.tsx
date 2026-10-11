import { LineChart } from './LineChart'

interface EvaluationChartProps {
  /**
   * The engine's evaluation after each of its moves, in pawns from White's side, oldest first.
   */
  history: number[]
}

/**
 * Positive means the engine thinks White is better; a forced mate sits at the edge.
 */
export function EvaluationChart({ history }: EvaluationChartProps) {
  return (
    <LineChart
      values={history}
      xLabel="Engine move"
      yLabel="Evaluation (pawns)"
      unit="pawns"
      domain={[-10, 10]}
      zeroLine
      format={(v) => `${v > 0 ? '+' : ''}${v.toFixed(Math.abs(v) < 10 && v % 1 !== 0 ? 1 : 0)}`}
    />
  )
}

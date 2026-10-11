import { LineChart } from './LineChart'

interface TimingChartProps {
  /**
   * Response times in ms, one per engine move, oldest first.
   */
  history: number[]
}

export function TimingChart({ history }: TimingChartProps) {
  return (
    <LineChart
      values={history.map((ms) => Math.round(ms))}
      xLabel="Move"
      yLabel="Response time (ms)"
      unit="ms"
      minimumTop={100}
      format={(v) => `${Math.round(v)}`}
    />
  )
}

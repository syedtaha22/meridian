import { LineChart } from './LineChart'

interface TimingChartProps {
  /**
   * Search time on the server in ms, one per engine move, oldest first.
   */
  searchHistory: number[]

  /**
   * Time from sending the request to receiving the reply in ms, aligned with `searchHistory`.
   */
  roundTripHistory: number[]
}

export function TimingChart({ searchHistory, roundTripHistory }: TimingChartProps) {
  return (
    <LineChart
      values={searchHistory.map((ms) => Math.round(ms))}
      secondary={{
        values: roundTripHistory.map((ms) => Math.round(ms)),
        yLabel: 'Round trip (ms)',
        legend: 'Round trip',
        unit: 'ms round trip',
        minimumTop: 100,
      }}
      xLabel="Move"
      yLabel="Search (ms)"
      legend="Search"
      unit="ms search"
      minimumTop={20}
      format={(v) => `${Math.round(v)}`}
    />
  )
}

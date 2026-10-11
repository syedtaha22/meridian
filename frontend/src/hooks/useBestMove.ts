import { useEffect, useState } from 'react'
import { requestAiMove } from '../lib/api-client'
import { moveToUci, uciToMove } from '../lib/chess-rules'
import type { EngineSettings, Move } from '../lib/types'

/**
 * The move the engine would play now, for the hint arrow. It is null while disabled or loading. A
 * result is shown only while its request is the current one.
 *
 * @param apiBaseUrl The address of the server.
 * @param startFen The FEN the game started from.
 * @param moves The moves played since.
 * @param enabled Whether the hint is wanted.
 * @param settings Depth and time limit.
 * @returns The move, or null.
 */
export function useBestMove(
  apiBaseUrl: string,
  startFen: string,
  moves: Move[],
  enabled: boolean,
  settings: EngineSettings,
): Move | null {
  const [answer, setAnswer] = useState<{ request: string; move: Move } | null>(null)
  const request = [
    apiBaseUrl,
    startFen,
    moves.map(moveToUci).join(' '),
    settings.depth,
    settings.timeLimitMs,
  ].join('|')

  useEffect(() => {
    if (!enabled) return

    let cancelled = false
    requestAiMove(apiBaseUrl, startFen, moves, settings)
      .then((result) => {
        if (!cancelled) setAnswer({ request, move: uciToMove(result.move) })
      })
      .catch(() => {
        // A hint is a visual aid, not core gameplay: a failed request means no arrow.
      })

    return () => {
      cancelled = true
    }
  }, [apiBaseUrl, startFen, moves, enabled, settings, request])

  return enabled && answer !== null && answer.request === request ? answer.move : null
}

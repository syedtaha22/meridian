import type { Score } from './types'

/**
 * A score for display, from the mover's side: "+0.35", "-1.20", "M3" or "-M2".
 *
 * @param score The engine score.
 * @returns The text.
 */
export function formatScore(score: Score): string {
  if (score.mate !== null) return `${score.mate < 0 ? '-' : ''}M${Math.abs(score.mate)}`
  const pawns = (score.cp ?? 0) / 100
  return `${pawns >= 0 ? '+' : ''}${pawns.toFixed(2)}`
}

/**
 * A score in pawns for charting. A forced mate sits at the limit.
 *
 * @param score The engine score.
 * @param limit Largest absolute value returned.
 * @returns Pawns, clipped to [-limit, limit].
 */
export function scoreToPawns(score: Score, limit = 10): number {
  if (score.mate !== null) return score.mate < 0 ? -limit : limit
  return Math.max(-limit, Math.min(limit, (score.cp ?? 0) / 100))
}

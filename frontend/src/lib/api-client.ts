import { moveToUci } from './chess-rules'
import type { EngineSettings, Move, Score } from './types'

export class ChessApiError extends Error {}

/**
 * Turns an error into a message for the player. A raw `fetch` failure (offline, wrong address,
 * CORS) becomes a plain sentence. A ChessApiError already carries the server's message.
 *
 * @param err The error that was thrown.
 * @returns A message for the player.
 */
export function friendlyErrorMessage(err: unknown): string {
  if (err instanceof ChessApiError) return err.message
  return "Couldn't reach the engine."
}

/**
 * The request body: the FEN the game started from plus the moves since. The server needs the move
 * list to see repeated positions and the fifty-move counter.
 *
 * @param startFen The FEN the game started from.
 * @param moves The moves played since.
 * @param settings Depth and time limit.
 * @returns The JSON body.
 */
function searchBody(startFen: string, moves: Move[], settings: EngineSettings) {
  return {
    fen: startFen,
    moves: moves.map(moveToUci),
    depth: settings.depth,
    time_limit_ms: settings.timeLimitMs,
  }
}

/**
 * Sends a JSON body to the engine.
 *
 * @param apiBaseUrl The address of the server.
 * @param path The path of the endpoint.
 * @param body The JSON body.
 * @returns The parsed JSON reply.
 * @throws ChessApiError When the server answers with an error status.
 */
async function post<T>(apiBaseUrl: string, path: string, body: unknown): Promise<T> {
  const response = await fetch(`${apiBaseUrl}${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })

  if (!response.ok) {
    throw new ChessApiError(`Chess backend returned ${response.status}`)
  }
  return (await response.json()) as T
}

export interface AiMove {
  /**
   * UCI, e.g. "e2e4" or "e7e8q".
   */
  move: string
  score: Score
  depth: number
  nodes: number
  elapsedMs: number
}

/**
 * Asks the engine for its move. The position should have a legal move, since the server answers
 * 422 otherwise. It also serves the hint arrow.
 *
 * @param apiBaseUrl The address of the server.
 * @param startFen The FEN the game started from.
 * @param moves The moves played since.
 * @param settings Depth and time limit.
 * @returns The move, its score and the search statistics.
 */
export async function requestAiMove(
  apiBaseUrl: string,
  startFen: string,
  moves: Move[],
  settings: EngineSettings,
): Promise<AiMove> {
  const data = await post<{
    move: string
    score: Score
    depth: number
    nodes: number
    elapsed_ms: number
  }>(apiBaseUrl, '/move', searchBody(startFen, moves, settings))
  return {
    move: data.move,
    score: data.score,
    depth: data.depth,
    nodes: data.nodes,
    elapsedMs: data.elapsed_ms,
  }
}

export interface EngineHealth {
  status: string
  engineVersion: string
  architecture: string
  maxDepth: number
  hashMb: number
}

/**
 * Reads the version and the limits of the engine.
 *
 * @param apiBaseUrl The address of the server.
 * @returns The engine's health.
 * @throws ChessApiError When the server answers with an error status.
 */
export async function checkHealth(apiBaseUrl: string): Promise<EngineHealth> {
  const response = await fetch(`${apiBaseUrl}/health`)
  if (!response.ok) throw new ChessApiError(`Chess backend returned ${response.status}`)
  const data = (await response.json()) as {
    status: string
    engine_version: string
    architecture: string
    max_depth: number
    hash_mb: number
  }
  return {
    status: data.status,
    engineVersion: data.engine_version,
    architecture: data.architecture,
    maxDepth: data.max_depth,
    hashMb: data.hash_mb,
  }
}

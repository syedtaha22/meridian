// Runs against a live backend. Skipped unless CHESS_API_URL is set, for example:
//   CHESS_API_URL=http://localhost:8080 npm test
import { describe, expect, it } from 'vitest'
import { ChessApiError, checkHealth, requestAiMove } from '../src/lib/api-client'
import {
  START_FEN,
  applyMove,
  createInitialPosition,
  findLegalMove,
  uciToMove,
} from '../src/lib/chess-rules'
import { DEFAULT_ENGINE_SETTINGS } from '../src/lib/types'

// `process` is not in the DOM typings this package compiles against.
const env = (globalThis as { process?: { env: Record<string, string | undefined> } }).process?.env
const apiBaseUrl = env?.CHESS_API_URL ?? ''

describe.skipIf(apiBaseUrl === '')('backend', () => {
  const settings = { depth: 3, timeLimitMs: 2000 }

  it('reports its health', async () => {
    const health = await checkHealth(apiBaseUrl)
    expect(health.status).toBe('ok')
    expect(health.maxDepth).toBeGreaterThan(0)
  })

  it('answers with a move the client rules accept', async () => {
    const reply = await requestAiMove(apiBaseUrl, START_FEN, [uciToMove('e2e4')], settings)
    const position = applyMove(createInitialPosition(), uciToMove('e2e4'))
    expect(findLegalMove(position, uciToMove(reply.move))).not.toBeNull()
    expect(reply.nodes).toBeGreaterThan(0)
  })

  it('finds a mate in one and reports it as a mate', async () => {
    const reply = await requestAiMove(
      apiBaseUrl,
      '6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1',
      [],
      DEFAULT_ENGINE_SETTINGS,
    )
    expect(reply.move).toBe('a1a8')
    expect(reply.score).toEqual({ cp: null, mate: 1 })
  })

  it('turns a rejected request into a ChessApiError', async () => {
    await expect(requestAiMove(apiBaseUrl, 'nonsense', [], settings)).rejects.toBeInstanceOf(
      ChessApiError,
    )
  })
})

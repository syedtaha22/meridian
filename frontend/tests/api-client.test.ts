import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  ChessApiError,
  checkHealth,
  friendlyErrorMessage,
  requestAiMove,
} from '../src/lib/api-client'
import { uciToMove } from '../src/lib/chess-rules'
import { DEFAULT_ENGINE_SETTINGS } from '../src/lib/types'

const START = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'

function mockFetch(body: unknown, ok = true, status = 200) {
  const fetchMock = vi.fn().mockResolvedValue({ ok, status, json: async () => body })
  vi.stubGlobal('fetch', fetchMock)
  return fetchMock
}

afterEach(() => vi.unstubAllGlobals())

describe('requestAiMove', () => {
  it('sends the start FEN with the move list and reads the snake_case reply', async () => {
    const fetchMock = mockFetch({
      move: 'e7e5',
      score: { cp: -12, mate: null },
      depth: 4,
      nodes: 1234,
      elapsed_ms: 56.5,
    })

    const result = await requestAiMove(
      'http://localhost:8080',
      START,
      [uciToMove('e2e4')],
      DEFAULT_ENGINE_SETTINGS,
    )

    expect(fetchMock).toHaveBeenCalledWith(
      'http://localhost:8080/move',
      expect.objectContaining({ method: 'POST' }),
    )
    expect(JSON.parse(fetchMock.mock.calls[0][1].body)).toEqual({
      fen: START,
      moves: ['e2e4'],
      depth: 4,
      time_limit_ms: 2000,
    })
    expect(result).toEqual({
      move: 'e7e5',
      score: { cp: -12, mate: null },
      depth: 4,
      nodes: 1234,
      elapsedMs: 56.5,
    })
  })

  it('throws a ChessApiError that keeps the status when the backend answers an error', async () => {
    mockFetch({}, false, 503)
    const attempt = requestAiMove('http://x', START, [], DEFAULT_ENGINE_SETTINGS)
    await expect(attempt).rejects.toBeInstanceOf(ChessApiError)
    await expect(attempt).rejects.toThrow('503')
  })
})

describe('checkHealth', () => {
  it('maps the health fields', async () => {
    mockFetch({
      status: 'ok',
      engine_version: '1.0.0',
      architecture: 'alpha-beta',
      max_depth: 12,
      hash_mb: 64,
    })
    expect(await checkHealth('http://x')).toEqual({
      status: 'ok',
      engineVersion: '1.0.0',
      architecture: 'alpha-beta',
      maxDepth: 12,
      hashMb: 64,
    })
  })
})

describe('friendlyErrorMessage', () => {
  it('keeps backend errors and replaces network failures', () => {
    expect(friendlyErrorMessage(new ChessApiError('Chess backend returned 500'))).toBe(
      'Chess backend returned 500',
    )
    expect(friendlyErrorMessage(new TypeError('Failed to fetch'))).toBe(
      "Couldn't reach the engine.",
    )
  })
})

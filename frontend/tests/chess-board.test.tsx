// @vitest-environment jsdom
import { act } from 'react'
import { createRoot } from 'react-dom/client'
import type { Root } from 'react-dom/client'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { ChessBoard } from '../src/components/board/ChessBoard'

// React only batches updates inside act() when it is told it is in a test.
;(globalThis as unknown as { IS_REACT_ACT_ENVIRONMENT: boolean }).IS_REACT_ACT_ENVIRONMENT = true

let container: HTMLDivElement
let root: Root
let fetchMock: ReturnType<typeof vi.fn>

function reply(body: unknown) {
  return Promise.resolve({ ok: true, status: 200, json: async () => body })
}

beforeEach(() => {
  container = document.createElement('div')
  document.body.appendChild(container)
  root = createRoot(container)

  fetchMock = vi.fn((url: string) => {
    if (url.endsWith('/health')) {
      return reply({
        status: 'ok',
        engine_version: '0.1.0',
        architecture: 'alpha-beta',
        max_depth: 12,
        hash_mb: 64,
      })
    }
    if (url.endsWith('/move')) {
      return reply({
        move: 'e7e5',
        score: { cp: 10, mate: null },
        depth: 4,
        nodes: 100,
        elapsed_ms: 5,
      })
    }
    return reply({ scores: [], depth: 1, elapsed_ms: 1 })
  })
  vi.stubGlobal('fetch', fetchMock)
  // jsdom has no media playback; the sounds are decoration anyway.
  vi.stubGlobal(
    'Audio',
    class {
      play() {
        return Promise.resolve()
      }
    },
  )
})

afterEach(() => {
  act(() => root.unmount())
  container.remove()
  vi.unstubAllGlobals()
})

async function flush() {
  await act(async () => {
    await Promise.resolve()
    await Promise.resolve()
  })
}

function square(name: string): HTMLButtonElement {
  const button = [...container.querySelectorAll('button')].find(
    (b) =>
      b.getAttribute('aria-label')?.startsWith(`${name},`) || b.getAttribute('aria-label') === name,
  )
  if (!button) throw new Error(`No square ${name}`)
  return button as HTMLButtonElement
}

/**
 * Dispatches a pointer event the way a browser would, at a position inside the board.
 */
function pointer(type: string, target: Element, clientX: number, clientY: number) {
  target.dispatchEvent(
    new MouseEvent(type, { bubbles: true, cancelable: true, clientX, clientY, button: 0 }),
  )
}

function board(): HTMLElement {
  return container.querySelector('[aria-label="Chess board"]') as HTMLElement
}

// The board is 800px wide in these tests, so a square is 100px and file f, row r is at
// ((f + 0.5) * 100, (r + 0.5) * 100).
function center(file: number, row: number): [number, number] {
  return [(file + 0.5) * 100, (row + 0.5) * 100]
}

describe('ChessBoard', () => {
  it('renders 64 squares with the initial pieces', async () => {
    await act(async () => root.render(<ChessBoard apiBaseUrl="http://api" />))
    await flush()

    expect(square('e1').getAttribute('aria-label')).toBe('e1, White king')
    expect(square('d8').getAttribute('aria-label')).toBe('d8, Black queen')
    expect(square('e4').getAttribute('aria-label')).toBe('e4')
    expect(container.textContent).toContain('Your move.')
  })

  it('plays a human move, then asks the backend for the reply', async () => {
    await act(async () => root.render(<ChessBoard apiBaseUrl="http://api" />))
    await flush()

    await act(async () => square('e2').click())
    expect(square('e4').getAttribute('aria-label')).toBe('e4, legal move')

    await act(async () => square('e4').click())
    await flush()

    const moveCall = fetchMock.mock.calls.find(([url]) => String(url).endsWith('/move'))
    expect(moveCall).toBeDefined()
    expect(JSON.parse(moveCall![1].body).moves).toEqual(['e2e4'])

    // The engine's e7e5 has been applied.
    expect(square('e5').getAttribute('aria-label')).toBe('e5, Black pawn')
    expect(container.textContent).toContain('e4')
  })

  it('does not move a piece to a square it cannot reach', async () => {
    await act(async () => root.render(<ChessBoard apiBaseUrl="http://api" />))
    await flush()

    await act(async () => square('e2').click())
    await act(async () => square('e5').click())

    expect(square('e2').getAttribute('aria-label')).toBe('e2, White pawn')
    expect(fetchMock.mock.calls.some(([url]) => String(url).endsWith('/move'))).toBe(false)
  })

  describe('copying the moves', () => {
    it('copies the numbered moves to the clipboard', async () => {
      const writeText = vi.fn().mockResolvedValue(undefined)
      Object.defineProperty(navigator, 'clipboard', { value: { writeText }, configurable: true })
      Object.defineProperty(window, 'isSecureContext', { value: true, configurable: true })

      await act(async () => root.render(<ChessBoard apiBaseUrl="http://api" />))
      await flush()
      const button = container.querySelector('button[aria-label="Copy moves"]') as HTMLButtonElement
      expect(button.disabled).toBe(true)

      await act(async () => square('e2').click())
      await act(async () => square('e4').click())
      await flush()
      expect(button.disabled).toBe(false)

      await act(async () => button.click())
      expect(writeText).toHaveBeenCalledWith('1. e4 e5')
      expect(button.getAttribute('aria-label')).toBe('Moves copied')
    })
  })

  describe('AI hints', () => {
    it('draws one arrow for the best move, only while it is the player turn', async () => {
      fetchMock.mockImplementation((url: string, init?: { body?: string }) => {
        if (url.endsWith('/health')) {
          return reply({
            status: 'ok',
            engine_version: '1',
            architecture: 'a',
            max_depth: 12,
            hash_mb: 8,
          })
        }
        const moves = JSON.parse(init?.body ?? '{}').moves as string[]
        return reply({
          move: moves.length === 0 ? 'g1f3' : 'e7e5',
          score: { cp: 20, mate: null },
          depth: 4,
          nodes: 10,
          elapsed_ms: 2,
        })
      })
      await act(async () => root.render(<ChessBoard apiBaseUrl="http://api" />))
      await flush()
      expect(container.querySelector('svg polygon')).toBeNull()

      const toggle = [...container.querySelectorAll('label')]
        .find((l) => l.textContent?.includes('Show AI hints'))!
        .querySelector('input') as HTMLInputElement
      await act(async () => toggle.click())
      await flush()

      // One arrow. g1 to f3 is a knight move, so the outline starts beside g1 and runs up
      // over g3.
      const arrows = container.querySelectorAll('.chess-arrow')
      expect(arrows).toHaveLength(1)
      const points = arrows[0].querySelector('polygon')!.getAttribute('points')!
      expect(points.startsWith('6.590,7.500 6.590,5.410')).toBe(true)
    })
  })

  describe('dragging', () => {
    beforeEach(() => {
      vi.spyOn(Element.prototype, 'getBoundingClientRect').mockImplementation(function (
        this: Element,
      ) {
        const size = this.getAttribute('aria-label') === 'Chess board' ? 800 : 0
        return {
          left: 0,
          top: 0,
          right: size,
          bottom: size,
          width: size,
          height: size,
          x: 0,
          y: 0,
        } as DOMRect
      })
    })

    afterEach(() => {
      vi.restoreAllMocks()
    })

    it('plays a move dropped on a legal square', async () => {
      await act(async () => root.render(<ChessBoard apiBaseUrl="http://api" />))
      await flush()

      const [fromX, fromY] = center(4, 6) // e2
      const [toX, toY] = center(4, 4) // e4
      await act(async () => pointer('pointerdown', board(), fromX, fromY))
      await act(async () => pointer('pointermove', board(), toX, toY))
      await act(async () => pointer('pointerup', board(), toX, toY))
      await flush()

      const moveCall = fetchMock.mock.calls.find(([url]) => String(url).endsWith('/move'))
      expect(moveCall).toBeDefined()
      expect(JSON.parse(moveCall![1].body).moves).toEqual(['e2e4'])
      expect(square('e4').getAttribute('aria-label')).toBe('e4, White pawn')
    })

    it('puts a piece back when it is dropped on an illegal square', async () => {
      await act(async () => root.render(<ChessBoard apiBaseUrl="http://api" />))
      await flush()

      const [fromX, fromY] = center(4, 6) // e2
      const [toX, toY] = center(4, 2) // e6, three squares away
      await act(async () => pointer('pointerdown', board(), fromX, fromY))
      await act(async () => pointer('pointermove', board(), toX, toY))
      await act(async () => pointer('pointerup', board(), toX, toY))
      await flush()

      expect(square('e2').getAttribute('aria-label')).toBe('e2, White pawn')
      expect(fetchMock.mock.calls.some(([url]) => String(url).endsWith('/move'))).toBe(false)
    })

    it('puts a piece back when it is dropped outside the board', async () => {
      await act(async () => root.render(<ChessBoard apiBaseUrl="http://api" />))
      await flush()

      const [fromX, fromY] = center(4, 6)
      await act(async () => pointer('pointerdown', board(), fromX, fromY))
      await act(async () => pointer('pointermove', board(), 900, 900))
      await act(async () => pointer('pointerup', board(), 900, 900))
      await flush()

      expect(square('e2').getAttribute('aria-label')).toBe('e2, White pawn')
      expect(fetchMock.mock.calls.some(([url]) => String(url).endsWith('/move'))).toBe(false)
    })

    it('treats a press without movement as a click and still allows click to move', async () => {
      await act(async () => root.render(<ChessBoard apiBaseUrl="http://api" />))
      await flush()

      const [fromX, fromY] = center(4, 6)
      const [toX, toY] = center(4, 4)
      await act(async () => pointer('pointerdown', board(), fromX, fromY))
      await act(async () => pointer('pointerup', board(), fromX, fromY))
      expect(square('e4').getAttribute('aria-label')).toBe('e4, legal move')

      await act(async () => pointer('pointerdown', board(), toX, toY))
      await act(async () => pointer('pointerup', board(), toX, toY))
      await flush()

      const moveCall = fetchMock.mock.calls.find(([url]) => String(url).endsWith('/move'))
      expect(JSON.parse(moveCall![1].body).moves).toEqual(['e2e4'])
    })

    it('does not pick up the opponent pieces', async () => {
      await act(async () => root.render(<ChessBoard apiBaseUrl="http://api" />))
      await flush()

      const [x, y] = center(4, 1) // e7
      await act(async () => pointer('pointerdown', board(), x, y))
      await act(async () => pointer('pointermove', board(), x, y + 200))
      await act(async () => pointer('pointerup', board(), x, y + 200))

      expect(square('e7').getAttribute('aria-label')).toBe('e7, Black pawn')
    })
  })
})

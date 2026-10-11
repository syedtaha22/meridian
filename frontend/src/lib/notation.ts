import type { GameState } from './types'

/**
 * The moves as numbered movetext, as in a PGN: "1. e4 e5 2. Nf3 Nc6". A finished game ends with
 * its result.
 *
 * @param state The game; only its moves, status and winner are read.
 * @returns The movetext, or an empty string when no move has been played.
 */
export function movesAsText(state: Pick<GameState, 'sans' | 'status' | 'winner'>): string {
  const parts: string[] = []
  state.sans.forEach((san, i) => {
    if (i % 2 === 0) parts.push(`${i / 2 + 1}.`)
    parts.push(san)
  })
  if (parts.length === 0) return ''

  if (state.status === 'checkmate') parts.push(state.winner === 'w' ? '1-0' : '0-1')
  else if (state.status === 'stalemate' || state.status === 'draw') parts.push('1/2-1/2')
  return parts.join(' ')
}

/**
 * Puts text on the clipboard, with a text-area fallback for older browsers and insecure pages.
 *
 * @param text The text to copy.
 * @returns True when it was copied.
 */
export async function copyText(text: string): Promise<boolean> {
  try {
    if (typeof navigator !== 'undefined' && navigator.clipboard && window.isSecureContext) {
      await navigator.clipboard.writeText(text)
      return true
    }
  } catch {
    // Fall through to the text area.
  }
  if (typeof document === 'undefined') return false
  const area = document.createElement('textarea')
  area.value = text
  area.setAttribute('readonly', '')
  area.style.position = 'fixed'
  area.style.opacity = '0'
  document.body.appendChild(area)
  area.select()
  let copied = false
  try {
    copied = document.execCommand('copy')
  } catch {
    copied = false
  }
  document.body.removeChild(area)
  return copied
}

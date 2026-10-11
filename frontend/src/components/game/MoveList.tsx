import { useEffect, useRef, useState } from 'react'
import { AlertIcon, CheckIcon, CopyIcon } from '../controls/icons'
import { copyText, movesAsText } from '../../lib/notation'
import type { GameState } from '../../lib/types'

interface MoveListProps {
  /**
   * The game whose moves are shown.
   */
  state: Pick<GameState, 'sans' | 'status' | 'winner'>
}

/**
 * The moves on one line under the board, with a button that copies them. The line scrolls
 * sideways and stays at the latest move.
 */
export function MoveList({ state }: MoveListProps) {
  const { sans } = state
  const ref = useRef<HTMLDivElement>(null)
  const [copied, setCopied] = useState<'idle' | 'copied' | 'failed'>('idle')

  useEffect(() => {
    const strip = ref.current
    if (strip) strip.scrollLeft = strip.scrollWidth
  }, [sans.length])

  // The confirmation goes away by itself.
  useEffect(() => {
    if (copied === 'idle') return
    const timer = setTimeout(() => setCopied('idle'), 1600)
    return () => clearTimeout(timer)
  }, [copied])

  const turns: { number: number; white: string; black: string | null }[] = []
  for (let i = 0; i < sans.length; i += 2) {
    turns.push({ number: i / 2 + 1, white: sans[i], black: sans[i + 1] ?? null })
  }

  const copy = async () => {
    setCopied((await copyText(movesAsText(state))) ? 'copied' : 'failed')
  }

  const label =
    copied === 'copied' ? 'Moves copied' : copied === 'failed' ? 'Could not copy' : 'Copy moves'

  return (
    <div className="chess-moves-row">
      <div className="chess-strip" ref={ref} aria-label="Moves">
        {turns.length === 0 ? (
          <span className="chess-strip-empty">Moves appear here.</span>
        ) : (
          turns.map((turn) => (
            <span key={turn.number} className="chess-strip-turn">
              <span className="chess-strip-number">{turn.number}.</span>
              <span>{turn.white}</span>
              {turn.black !== null && <span>{turn.black}</span>}
            </span>
          ))
        )}
      </div>
      <button
        type="button"
        className={`chess-copy${copied === 'copied' ? ' is-copied' : ''}`}
        onClick={copy}
        disabled={sans.length === 0}
        aria-label={label}
        title={label}
      >
        {copied === 'copied' ? <CheckIcon /> : copied === 'failed' ? <AlertIcon /> : <CopyIcon />}
      </button>
    </div>
  )
}

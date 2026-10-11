import { useCallback, useEffect, useRef, useState } from 'react'
import {
  advanceTurn,
  createInitialState,
  findLegalMove,
  opposite,
  uciToMove,
} from '../lib/chess-rules'
import { friendlyErrorMessage, requestAiMove } from '../lib/api-client'
import type { AiMove } from '../lib/api-client'
import { playSound } from '../lib/sounds'
import type { Color, EngineSettings, GameState, Move } from '../lib/types'

/**
 * The number of pieces on the board.
 *
 * @param state The game state.
 * @returns The count.
 */
function countPieces(state: GameState): number {
  return state.position.board.filter((cell) => cell !== '').length
}

/**
 * The state of a game against the engine, and the engine's replies to the human's moves.
 *
 * @param apiBaseUrl The address of the server.
 * @param humanColor The color the human plays.
 * @param settings Depth and time limit of the engine.
 * @param onSearch Called with the result of each search of the engine.
 * @param soundEnabled Whether move sounds play.
 * @returns The game state, whether the engine is thinking, the last error, the function to play a
 *   human move, and the function to restart.
 */
export function useChessGame(
  apiBaseUrl: string,
  humanColor: Color,
  settings: EngineSettings,
  onSearch: (result: AiMove) => void,
  soundEnabled: boolean = true,
) {
  const [state, setState] = useState<GameState>(() => createInitialState())
  // A failure belongs to the position and settings it happened with, so changing either
  // (a new attempt) clears it without an effect having to reset anything.
  const [failure, setFailure] = useState<{
    message: string
    state: GameState
    settings: EngineSettings
  } | null>(null)
  const error =
    failure !== null && failure.state === state && failure.settings === settings
      ? failure.message
      : null

  const aiColor = opposite(humanColor)
  // Derived: true while the engine's request is unresolved. A played move changes the turn, and an
  // error ends it too.
  const aiThinking = state.status === 'playing' && state.position.turn === aiColor && error === null

  useEffect(() => {
    if (state.status !== 'playing' || state.position.turn !== aiColor) return

    let cancelled = false
    const requestedAt = state.moves.length

    requestAiMove(apiBaseUrl, state.startFen, state.moves, settings)
      .then((result) => {
        if (cancelled) return
        onSearch(result)
        const move = findLegalMove(state.position, uciToMove(result.move))
        if (move === null) {
          setFailure({
            message: `The engine sent an illegal move: ${result.move}`,
            state,
            settings,
          })
          return
        }
        // Guards against a restart or a settings change racing the response.
        setState((prev) => (prev.moves.length === requestedAt ? advanceTurn(prev, move) : prev))
      })
      .catch((err: unknown) => {
        if (!cancelled) setFailure({ message: friendlyErrorMessage(err), state, settings })
      })

    return () => {
      cancelled = true
    }
    // Changing settings mid-search cancels the request and starts it again with the new settings.
    // `onSearch` is only a notification, and it should be stable.
  }, [apiBaseUrl, aiColor, state, settings, onSearch])

  const playHumanMove = useCallback(
    (move: Move) => {
      setState((prev) => {
        if (prev.status !== 'playing' || prev.position.turn !== humanColor) return prev
        const legal = findLegalMove(prev.position, move)
        return legal ? advanceTurn(prev, legal) : prev
      })
    },
    [humanColor],
  )

  const restart = useCallback(() => {
    setState(createInitialState())
    setFailure(null)
  }, [])

  // Sound effects follow the state, so a move sounds alike whoever made it.
  const previous = useRef<GameState | null>(null)
  useEffect(() => {
    const before = previous.current
    previous.current = state
    if (!soundEnabled || before === state) return
    if (before === null || state.moves.length === 0) {
      playSound('start')
      return
    }
    if (state.moves.length === before.moves.length) return
    if (state.status !== 'playing') playSound('end')
    else if (state.inCheck) playSound('check')
    else if (countPieces(state) < countPieces(before)) playSound('capture')
    else playSound('move')
  }, [state, soundEnabled])

  return { state, aiThinking, error, playHumanMove, restart }
}

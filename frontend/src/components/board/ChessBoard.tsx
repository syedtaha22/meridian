'use client'

import '../../chess.css'
import { useCallback, useMemo, useRef, useState } from 'react'
import type { PointerEvent } from 'react'
import { useChessGame } from '../../hooks/useChessGame'
import { useBestMove } from '../../hooks/useBestMove'
import { colorOf, getLegalMoves, isPromotionMove, squareName } from '../../lib/chess-rules'
import type { AiMove } from '../../lib/api-client'
import { scoreToPawns } from '../../lib/score'
import { DEFAULT_ENGINE_SETTINGS } from '../../lib/types'
import type { Color, EngineSettings, Move, Piece, PromotionPiece } from '../../lib/types'
import { Arrow } from './Arrow'
import { EngineCard } from '../game/EngineCard'
import { EvaluationChart } from '../charts/EvaluationChart'
import { GameSettings } from '../game/GameSettings'
import { GameStatusBar } from '../game/GameStatusBar'
import { MoveList } from '../game/MoveList'
import { PieceIcon } from './PieceIcon'
import { PieceSprite } from './PieceSprite'
import { PromotionPicker } from './PromotionPicker'
import { Square } from './Square'
import { TimingChart } from '../charts/TimingChart'

interface ChessBoardProps {
  apiBaseUrl: string
}

export function ChessBoard({ apiBaseUrl }: ChessBoardProps) {
  const [humanColor, setHumanColor] = useState<Color>('w')
  const [settings, setSettings] = useState<EngineSettings>(DEFAULT_ENGINE_SETTINGS)
  const [showMoveHints, setShowMoveHints] = useState(true)
  const [showAiHints, setShowAiHints] = useState(false)
  const [soundEnabled, setSoundEnabled] = useState(true)
  const [latencyHistory, setLatencyHistory] = useState<number[]>([])
  const [evaluationHistory, setEvaluationHistory] = useState<number[]>([])

  const clearHistory = useCallback(() => {
    setLatencyHistory([])
    setEvaluationHistory([])
  }, [])

  const handleHumanColorChange = (color: Color) => {
    setHumanColor(color)
    clearHistory()
  }

  // Stable unless the human's color changes. The engine-move effect in useChessGame depends on it,
  // and a new reference would send the request again on every render.
  const recordSearch = useCallback(
    (result: AiMove) => {
      setLatencyHistory((prev) => [...prev, result.elapsedMs])
      // The engine's score is from its own side to move; the chart is
      // always from White's side so the line reads identically either way.
      const pawns = scoreToPawns(result.score)
      setEvaluationHistory((prev) => [...prev, humanColor === 'w' ? -pawns : pawns])
    },
    [humanColor],
  )

  const settingsPanel = (
    <GameSettings
      humanColor={humanColor}
      onHumanColorChange={handleHumanColorChange}
      settings={settings}
      onSettingsChange={setSettings}
      showMoveHints={showMoveHints}
      onShowMoveHintsChange={setShowMoveHints}
      showAiHints={showAiHints}
      onShowAiHintsChange={setShowAiHints}
      soundEnabled={soundEnabled}
      onSoundEnabledChange={setSoundEnabled}
    />
  )

  return (
    <>
      <div className="chess-layout">
        <aside className="chess-side chess-side-left">
          <EngineCard apiBaseUrl={apiBaseUrl} />
        </aside>

        <div className="chess-main">
          {/* Keyed by humanColor: switching sides mid-game would otherwise leave
              stale, inconsistent turn state (whose turn is it now?), so remount
              the whole game fresh instead of trying to patch it in place. */}
          <Game
            key={humanColor}
            apiBaseUrl={apiBaseUrl}
            humanColor={humanColor}
            settings={settings}
            showMoveHints={showMoveHints}
            showAiHints={showAiHints}
            soundEnabled={soundEnabled}
            onSearch={recordSearch}
            onRestart={clearHistory}
          />
        </div>

        <aside className="chess-side chess-side-right">{settingsPanel}</aside>
      </div>

      <section className="chess-diagnostics">
        <h2>Search diagnostics</h2>
        <div className="chess-charts">
          <div>
            <h3>Response time</h3>
            <p className="chess-muted">
              How long each of the engine&apos;s searches took, move by move.
            </p>
            <TimingChart history={latencyHistory} />
          </div>
          <div>
            <h3>Evaluation</h3>
            <p className="chess-muted">
              The engine&apos;s evaluation after each of its moves, in pawns from White&apos;s side.
            </p>
            <EvaluationChart history={evaluationHistory} />
          </div>
        </div>
      </section>
    </>
  )
}

interface GameProps {
  apiBaseUrl: string
  humanColor: Color
  settings: EngineSettings
  showMoveHints: boolean
  showAiHints: boolean
  soundEnabled: boolean
  onSearch: (result: AiMove) => void
  onRestart: () => void
}

/**
 * Pixels a press has to travel before it counts as a drag rather than a click.
 */
const DRAG_THRESHOLD = 4

interface DragState {
  /**
   * Square the piece was picked up from.
   */
  from: number

  /**
   * Pointer position relative to the board, in pixels.
   */
  x: number
  y: number

  /**
   * Width of one square, in pixels, measured when the piece was picked up.
   */
  size: number
  moved: boolean

  /**
   * The piece was already the selected one when it was pressed.
   */
  wasSelected: boolean

  /**
   * Square under the pointer, or null outside the board.
   */
  over: number | null
}

function Game({
  apiBaseUrl,
  humanColor,
  settings,
  showMoveHints,
  showAiHints,
  soundEnabled,
  onSearch,
  onRestart,
}: GameProps) {
  const { state, aiThinking, error, playHumanMove, restart } = useChessGame(
    apiBaseUrl,
    humanColor,
    settings,
    onSearch,
    soundEnabled,
  )
  const [selected, setSelected] = useState<number | null>(null)
  const [pendingPromotion, setPendingPromotion] = useState<Move | null>(null)
  const [drag, setDrag] = useState<DragState | null>(null)
  const boardRef = useRef<HTMLDivElement>(null)

  const isHumanTurn = state.status === 'playing' && state.position.turn === humanColor
  const legalMoves = useMemo(
    () => (isHumanTurn ? getLegalMoves(state.position) : []),
    [isHumanTurn, state.position],
  )
  const selectedMoves = selected === null ? [] : legalMoves.filter((m) => m.from === selected)

  // "AI hints" is what the engine would play for *you*, so it only runs during your
  // turn: there is nothing to recommend while the engine is deciding its own move.
  const bestMove = useBestMove(
    apiBaseUrl,
    state.startFen,
    state.moves,
    showAiHints && isHumanTurn,
    settings,
  )

  /**
   * Square under a pointer event, or null when it is outside the board.
   */
  const squareAt = (event: { clientX: number; clientY: number }): number | null => {
    const board = boardRef.current
    if (board === null) return null
    const rect = board.getBoundingClientRect()
    const col = Math.floor(((event.clientX - rect.left) / rect.width) * 8)
    const row = Math.floor(((event.clientY - rect.top) / rect.height) * 8)
    if (col < 0 || col > 7 || row < 0 || row > 7) return null
    const index = row * 8 + col
    return humanColor === 'w' ? index : 63 - index
  }

  const isOwnMovablePiece = (square: number): boolean => {
    const piece = state.position.board[square]
    return (
      piece !== '' && colorOf(piece) === humanColor && legalMoves.some((m) => m.from === square)
    )
  }

  /**
   * Plays the selected piece to `square` when that is legal, asking for a promotion piece first.
   */
  const tryMove = (from: number, square: number): boolean => {
    if (!legalMoves.some((m) => m.from === from && m.to === square)) return false
    if (isPromotionMove(state.position, from, square)) {
      setPendingPromotion({ from, to: square })
    } else {
      playHumanMove({ from, to: square })
    }
    setSelected(null)
    return true
  }

  const handlePointerDown = (event: PointerEvent<HTMLDivElement>) => {
    if (!isHumanTurn || pendingPromotion || event.button !== 0) return
    const square = squareAt(event)
    if (square === null) return

    // A piece is already picked up and this square is a destination: a click-click move.
    if (selected !== null && tryMove(selected, square)) return

    if (isOwnMovablePiece(square)) {
      const rect = boardRef.current!.getBoundingClientRect()
      setDrag({
        from: square,
        x: event.clientX - rect.left,
        y: event.clientY - rect.top,
        size: rect.width / 8,
        moved: false,
        wasSelected: selected === square,
        over: square,
      })
      setSelected(square)
      // Keeps the move and release events coming while the pointer leaves the board.
      boardRef.current?.setPointerCapture?.(event.pointerId)
    } else {
      setSelected(null)
    }
  }

  const handlePointerMove = (event: PointerEvent<HTMLDivElement>) => {
    if (drag === null) return
    const rect = boardRef.current!.getBoundingClientRect()
    const x = event.clientX - rect.left
    const y = event.clientY - rect.top
    const travelled = Math.hypot(x - drag.x, y - drag.y)
    setDrag({
      ...drag,
      x,
      y,
      moved: drag.moved || travelled > DRAG_THRESHOLD,
      over: squareAt(event),
    })
  }

  const handlePointerUp = (event: PointerEvent<HTMLDivElement>) => {
    if (drag === null) return
    boardRef.current?.releasePointerCapture?.(event.pointerId)
    const target = squareAt(event)
    setDrag(null)

    if (!drag.moved) {
      // A click on the piece: it stays picked up, and a second click puts it down.
      if (drag.wasSelected) setSelected(null)
      return
    }
    // A drop. A legal destination plays the move; anywhere else sends the piece back.
    if (target === null || target === drag.from || !tryMove(drag.from, target)) {
      setSelected(null)
    }
  }

  /**
   * Keyboard activation (Enter or Space on a focused square); pointers go through the board.
   */
  const handleKeyboardActivate = (square: number) => {
    if (!isHumanTurn || pendingPromotion) return
    if (selected !== null && tryMove(selected, square)) return
    setSelected(isOwnMovablePiece(square) && square !== selected ? square : null)
  }

  const handlePromotion = (piece: PromotionPiece) => {
    if (pendingPromotion) playHumanMove({ ...pendingPromotion, promotion: piece })
    setPendingPromotion(null)
    setSelected(null)
  }

  const handleRestart = () => {
    restart()
    onRestart()
    setSelected(null)
    setPendingPromotion(null)
    setDrag(null)
  }

  const squares = Array.from({ length: 64 }, (_, i) => (humanColor === 'w' ? i : 63 - i))
  const squareSize = drag?.size ?? 0
  const grabbing = drag !== null && drag.moved

  return (
    <>
      <PieceSprite />
      <GameStatusBar state={state} humanColor={humanColor} aiThinking={aiThinking} error={error} />

      <div
        ref={boardRef}
        role="group"
        aria-label="Chess board"
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerCancel={() => {
          setDrag(null)
          setSelected(null)
        }}
        className="chess-board"
        style={{ cursor: grabbing ? 'grabbing' : 'default' }}
      >
        <div className="chess-board-glass">
          {squares.map((square, displayIndex) => {
            const row = square >> 3
            const col = square & 7
            const displayRow = displayIndex >> 3
            const displayCol = displayIndex & 7
            const piece = state.position.board[square]

            const targetMove = selectedMoves.find((m) => m.to === square)
            const isLegalTarget = showMoveHints && targetMove !== undefined

            return (
              <Square
                key={square}
                piece={piece}
                name={squareName(square)}
                isLight={(row + col) % 2 === 0}
                isLastMove={
                  state.lastMove !== null &&
                  (state.lastMove.from === square || state.lastMove.to === square)
                }
                isLegalTarget={isLegalTarget}
                isHintSource={bestMove !== null && drag === null && bestMove.from === square}
                isDragSource={drag !== null && drag.moved && drag.from === square}
                isDropTarget={
                  drag !== null && drag.moved && drag.over === square && targetMove !== undefined
                }
                isSelected={selected === square}
                isMovable={isOwnMovablePiece(square)}
                coordinates={{
                  rank: displayCol === 0 ? String(8 - row) : null,
                  file: displayRow === 7 ? 'abcdefgh'[col] : null,
                }}
                onClick={(e) => {
                  // Pointer clicks arrive with a detail of 1 or more and were handled on
                  // press; a detail of 0 is the keyboard.
                  if (e.detail === 0) handleKeyboardActivate(square)
                }}
              />
            )
          })}
          {bestMove !== null && drag === null && (
            <Arrow from={bestMove.from} to={bestMove.to} orientation={humanColor} />
          )}
        </div>

        {/* The piece under the pointer. It follows the pointer's center like the
            dragged piece of the original board, and is not interactive. */}
        {drag !== null && drag.moved && squareSize > 0 && (
          <div
            style={{
              position: 'absolute',
              left: drag.x - squareSize / 2,
              top: drag.y - squareSize / 2,
              width: squareSize,
              height: squareSize,
              pointerEvents: 'none',
              zIndex: 10,
            }}
          >
            <PieceIcon piece={state.position.board[drag.from] as Piece} />
          </div>
        )}
      </div>

      {pendingPromotion && (
        <div className="chess-promotion-row">
          <PromotionPicker
            color={humanColor}
            onPick={handlePromotion}
            onCancel={() => setPendingPromotion(null)}
          />
        </div>
      )}

      <MoveList state={state} />

      <div className="chess-actions">
        <button type="button" className="chess-button" onClick={handleRestart}>
          New game
        </button>
      </div>
    </>
  )
}

export type SoundKind = 'start' | 'move' | 'capture' | 'check' | 'end'

const FILES: Record<SoundKind, URL> = {
  start: new URL('../assets/audio/GameStart.wav', import.meta.url),
  move: new URL('../assets/audio/ChessPiecePlaced.wav', import.meta.url),
  capture: new URL('../assets/audio/ChessPieceCaptured.wav', import.meta.url),
  check: new URL('../assets/audio/KingChecked.wav', import.meta.url),
  end: new URL('../assets/audio/GameEnd.wav', import.meta.url),
}

/**
 * Plays a short effect. Silent without `Audio` (server rendering, tests) or when the browser
 * refuses playback, so a missing sound does not stop a move.
 *
 * @param kind The effect to play.
 */
export function playSound(kind: SoundKind): void {
  if (typeof Audio === 'undefined') return
  try {
    void new Audio(FILES[kind].href).play().catch(() => {})
  } catch {
    // Playback is decoration; nothing to recover.
  }
}

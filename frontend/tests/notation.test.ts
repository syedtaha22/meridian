import { describe, expect, it } from 'vitest'
import { movesAsText } from '../src/lib/notation'

const base = { status: 'playing' as const, winner: null }

describe('movesAsText', () => {
  it('is empty before the first move', () => {
    expect(movesAsText({ ...base, sans: [] })).toBe('')
  })

  it('numbers each pair and keeps a lone white move', () => {
    expect(movesAsText({ ...base, sans: ['e4', 'e5', 'Nf3'] })).toBe('1. e4 e5 2. Nf3')
    expect(movesAsText({ ...base, sans: ['e4', 'e5', 'Nf3', 'Nc6'] })).toBe('1. e4 e5 2. Nf3 Nc6')
  })

  it('ends a finished game with its result', () => {
    const fools = ['f3', 'e5', 'g4', 'Qh4#']
    expect(movesAsText({ sans: fools, status: 'checkmate', winner: 'b' })).toBe(
      '1. f3 e5 2. g4 Qh4# 0-1',
    )
    expect(movesAsText({ sans: ['e4'], status: 'checkmate', winner: 'w' })).toBe('1. e4 1-0')
    expect(movesAsText({ sans: ['Kb6'], status: 'stalemate', winner: null })).toBe('1. Kb6 1/2-1/2')
    expect(movesAsText({ sans: ['Kb6'], status: 'draw', winner: null })).toBe('1. Kb6 1/2-1/2')
  })
})

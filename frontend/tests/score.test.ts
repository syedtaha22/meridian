import { describe, expect, it } from 'vitest'
import { formatScore, scoreToPawns } from '../src/lib/score'

describe('formatScore', () => {
  it('shows pawns with a sign and mates with an M', () => {
    expect(formatScore({ cp: 35, mate: null })).toBe('+0.35')
    expect(formatScore({ cp: -120, mate: null })).toBe('-1.20')
    expect(formatScore({ cp: 0, mate: null })).toBe('+0.00')
    expect(formatScore({ cp: null, mate: 3 })).toBe('M3')
    expect(formatScore({ cp: null, mate: -2 })).toBe('-M2')
  })
})

describe('scoreToPawns', () => {
  it('clips to the chart limit and pins mates to it', () => {
    expect(scoreToPawns({ cp: 250, mate: null })).toBe(2.5)
    expect(scoreToPawns({ cp: 5000, mate: null })).toBe(10)
    expect(scoreToPawns({ cp: null, mate: -1 })).toBe(-10)
    expect(scoreToPawns({ cp: null, mate: 4 })).toBe(10)
  })
})

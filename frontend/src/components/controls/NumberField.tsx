import { useState } from 'react'

interface NumberFieldProps {
  value: number
  onChange: (value: number) => void
  min: number
  max: number
  step?: number
}

function clamp(n: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, n))
}

/**
 * A number input with its own string state, so that clearing the field does not become 0
 * mid-edit. The value is pushed up once it is in range, and corrected on blur.
 */
export function NumberField({ value, onChange, min, max, step = 1 }: NumberFieldProps) {
  const [text, setText] = useState(String(value))

  // Syncs with `value` when a parent changes it. This happens while rendering, which React allows
  // for state derived from props.
  const [seen, setSeen] = useState(value)
  if (value !== seen) {
    setSeen(value)
    if (Number(text) !== value) setText(String(value))
  }

  return (
    <input
      className="chess-input"
      type="number"
      min={min}
      max={max}
      step={step}
      value={text}
      onChange={(e) => {
        const raw = e.target.value
        setText(raw)
        const parsed = Number(raw)
        if (raw.trim() !== '' && !Number.isNaN(parsed) && parsed >= min && parsed <= max) {
          onChange(parsed)
        }
      }}
      onBlur={() => {
        const parsed = Number(text)
        const next = Number.isNaN(parsed) ? min : clamp(parsed, min, max)
        setText(String(next))
        onChange(next)
      }}
    />
  )
}

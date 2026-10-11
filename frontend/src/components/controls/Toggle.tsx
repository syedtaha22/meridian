import type { ReactNode } from 'react'

interface ToggleProps {
  checked: boolean
  onChange: (checked: boolean) => void
  children: ReactNode
}

/**
 * A switch: the native checkbox stays for keyboard and screen readers and is hidden visually.
 */
export function Toggle({ checked, onChange, children }: ToggleProps) {
  return (
    <label className="chess-toggle">
      <span>{children}</span>
      <span className="chess-toggle-track">
        <input type="checkbox" checked={checked} onChange={(e) => onChange(e.target.checked)} />
        <span className="chess-toggle-thumb" />
      </span>
    </label>
  )
}

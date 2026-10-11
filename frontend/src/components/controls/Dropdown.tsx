import { useEffect, useRef, useState } from 'react'

interface DropdownOption<T extends string | number> {
  value: T
  label: string
}

interface DropdownProps<T extends string | number> {
  value: T
  options: DropdownOption<T>[]
  onChange: (value: T) => void
}

/**
 * A button and a panel. The open list takes its colors from the host's custom properties.
 */
export function Dropdown<T extends string | number>({
  value,
  options,
  onChange,
}: DropdownProps<T>) {
  const [open, setOpen] = useState(false)
  const rootRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (!open) return
    function onPointerDown(e: PointerEvent) {
      if (rootRef.current && !rootRef.current.contains(e.target as Node)) setOpen(false)
    }
    document.addEventListener('pointerdown', onPointerDown)
    return () => document.removeEventListener('pointerdown', onPointerDown)
  }, [open])

  const current = options.find((o) => o.value === value)

  return (
    <div ref={rootRef} className="chess-dropdown">
      <button
        type="button"
        className="chess-dropdown-trigger"
        aria-haspopup="listbox"
        aria-expanded={open}
        onClick={() => setOpen((v) => !v)}
      >
        <span>{current?.label}</span>
        <svg aria-hidden="true" viewBox="0 0 12 8" width="10" height="7">
          <path
            d="M1 1l5 5 5-5"
            stroke="currentColor"
            strokeWidth="1.6"
            fill="none"
            strokeLinecap="round"
            strokeLinejoin="round"
          />
        </svg>
      </button>

      {open && (
        <div className="chess-dropdown-menu" role="listbox">
          {options.map((option) => (
            <button
              key={option.value}
              type="button"
              role="option"
              aria-selected={option.value === value}
              className={option.value === value ? 'is-active' : undefined}
              onClick={() => {
                onChange(option.value)
                setOpen(false)
              }}
            >
              {option.label}
            </button>
          ))}
        </div>
      )}
    </div>
  )
}

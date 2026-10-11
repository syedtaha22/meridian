import type { Color, EngineSettings } from '../../lib/types'
import { Dropdown } from '../controls/Dropdown'
import { NumberField } from '../controls/NumberField'
import { Toggle } from '../controls/Toggle'

interface GameSettingsProps {
  humanColor: Color
  onHumanColorChange: (color: Color) => void
  settings: EngineSettings
  onSettingsChange: (settings: EngineSettings) => void
  showMoveHints: boolean
  onShowMoveHintsChange: (show: boolean) => void
  showAiHints: boolean
  onShowAiHintsChange: (show: boolean) => void
  soundEnabled: boolean
  onSoundEnabledChange: (enabled: boolean) => void
}

const COLOR_OPTIONS: { value: Color; label: string }[] = [
  { value: 'w', label: 'White (moves first)' },
  { value: 'b', label: 'Black' },
]

export function GameSettings({
  humanColor,
  onHumanColorChange,
  settings,
  onSettingsChange,
  showMoveHints,
  onShowMoveHintsChange,
  showAiHints,
  onShowAiHintsChange,
  soundEnabled,
  onSoundEnabledChange,
}: GameSettingsProps) {
  return (
    <div className="chess-side-panel">
      <h3 className="chess-panel-title">Settings</h3>

      <div className="chess-fields">
        <label className="chess-field">
          Play as
          <Dropdown value={humanColor} options={COLOR_OPTIONS} onChange={onHumanColorChange} />
        </label>

        <label className="chess-field">
          Search depth (plies)
          <NumberField
            min={1}
            max={12}
            value={settings.depth}
            onChange={(n) => onSettingsChange({ ...settings, depth: n })}
          />
        </label>

        <label className="chess-field">
          Time limit per move (ms, 0 for none)
          <NumberField
            min={0}
            max={60000}
            step={100}
            value={settings.timeLimitMs}
            onChange={(n) => onSettingsChange({ ...settings, timeLimitMs: n })}
          />
        </label>

        <div className="chess-toggles">
          <div>
            <Toggle checked={showMoveHints} onChange={onShowMoveHintsChange}>
              Show move hints
            </Toggle>
            <p className="chess-hint">Marks every square the selected piece can legally reach.</p>
          </div>
          <div>
            <Toggle checked={showAiHints} onChange={onShowAiHintsChange}>
              Show AI hints
            </Toggle>
            <p className="chess-hint">
              Draws an arrow for the move the engine would play in your place.
            </p>
          </div>
          <div>
            <Toggle checked={soundEnabled} onChange={onSoundEnabledChange}>
              Sound
            </Toggle>
          </div>
        </div>
      </div>
    </div>
  )
}

# Chess board

This is a chess board as a React component. It has its own rules engine for move validation, and
any page can mount it. It sends requests to the engine in [backend](../backend/README.md) over
HTTP, and it includes its own stylesheet.

```bash
frontend
├── scripts
│   └── build-piece-sprite.mjs      # builds the piece sprite from the drawings
├── src
│   ├── assets
│   │   ├── audio                   # the five move sounds
│   │   └── pieces                  # one SVG per black piece: bking, bpawn, ...
│   ├── components
│   │   ├── board
│   │   │   ├── ChessBoard.tsx      # the board and everything around it
│   │   │   ├── Square.tsx
│   │   │   ├── Arrow.tsx           # the hint arrow
│   │   │   └── PromotionPicker.tsx
│   │   ├── game
│   │   │   ├── MoveList.tsx        # the moves, with a button to copy them
│   │   │   ├── GameSettings.tsx
│   │   │   ├── GameStatusBar.tsx
│   │   │   └── EngineCard.tsx
│   │   ├── charts
│   │   │   └── LineChart.tsx       # the timing and evaluation charts
│   │   └── controls                # dropdown, number field, toggle, icons
│   ├── hooks
│   │   ├── useChessGame.ts         # game state and the engine's replies
│   │   └── useBestMove.ts          # the move shown by the hint arrow
│   ├── lib
│   │   ├── chess-rules.ts          # legal moves, check, mate, draws, perft
│   │   ├── api-client.ts           # requests to the engine
│   │   └── piece-sprite.ts         # generated, do not edit
│   ├── chess.css                   # every style, each class prefixed chess-
│   └── index.ts                    # exports ChessBoard
└── tests                           # rules, notation, scores, the board in jsdom
```

## Usage

```tsx
import { ChessBoard } from '@chess/frontend'

<ChessBoard apiBaseUrl="https://engine.example.com" />
```

`apiBaseUrl` is the address of the server. The component imports its own
[stylesheet](src/chess.css), so the page only has to compile the package. In Next.js, add
`transpilePackages: ["@chess/frontend"]`.

## Theme

The board reads its colors and fonts from CSS custom properties of the host page. Every use has
a fallback value. The properties are `--paper`, `--card`, `--ink`, `--ink-soft`, `--muted`,
`--rule`, `--rule-soft`, `--accent`, `--accent-ink`, `--on-ink`, `--on-accent`,
`--glass-border`, `--sans` and `--mono`.

The dark theme is selected with `data-theme="dark"` on the `html` element. The glass effect of
the board uses the custom properties `--chess-*` on `.chess-board`.

## Development

```bash
npm ci
npm test          # rules, notation, scores, the board in jsdom
npm run lint
npm run format:check   # Prettier, print width 100
npx tsc --noEmit
```

After changing a piece drawing, run `npm run build:pieces`. It regenerates
[piece-sprite.ts](src/lib/piece-sprite.ts) and formats it. The white pieces are the black
drawings with lighter shading colors, so there are six drawings and not twelve.

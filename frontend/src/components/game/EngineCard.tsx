'use client'

import { useEffect, useState } from 'react'
import { checkHealth } from '../../lib/api-client'
import type { EngineHealth } from '../../lib/api-client'

interface EngineCardProps {
  apiBaseUrl: string
}

/**
 * Facts about the engine from its health endpoint. It is drawn before the engine answers, so the
 * column keeps its place when the engine is unreachable.
 */
export function EngineCard({ apiBaseUrl }: EngineCardProps) {
  const [health, setHealth] = useState<EngineHealth | null>(null)
  const [offline, setOffline] = useState(false)

  useEffect(() => {
    let cancelled = false
    checkHealth(apiBaseUrl)
      .then((result) => {
        if (!cancelled) {
          setHealth(result)
          setOffline(false)
        }
      })
      .catch(() => {
        if (!cancelled) setOffline(true)
      })
    return () => {
      cancelled = true
    }
  }, [apiBaseUrl])

  return (
    <div className="chess-side-panel">
      <h3 className="chess-panel-title">About this engine</h3>
      {offline && !health ? (
        <p className="chess-muted">The engine cannot be reached right now.</p>
      ) : (
        <dl className="chess-facts">
          <div>
            <dt>Search</dt>
            <dd>{health ? health.architecture : '...'}</dd>
          </div>
          <div>
            <dt>Version</dt>
            <dd>{health ? health.engineVersion : '...'}</dd>
          </div>
          <div>
            <dt>Maximum depth</dt>
            <dd>{health ? health.maxDepth : '...'}</dd>
          </div>
          <div>
            <dt>Hash table</dt>
            <dd>{health ? `${health.hashMb} MB` : '...'}</dd>
          </div>
        </dl>
      )}
    </div>
  )
}

/**
 * @file Live stream session store shared by the shell (live strip, host status) and pages.
 *
 * Polls GET /api/sessions every 2 s while at least one component uses it and the tab is visible.
 * A 404 means the host has no sessions API yet: the store reports `available: false` quietly and
 * retries once a minute. Any other failure keeps the last data and sets `error`.
 *
 * Session shape (host contract):
 *   { id, client_uuid, client_name, app_name, started_at (unix s), resolution: { w, h }, fps_actual,
 *     fps_requested?, codec, bitrate_kbps, latency_ms: { capture, encode, send, total }, loss_pct,
 *     display: { kind: "desktop" } | { kind: "virtual", name: ":20", w, h, fps },
 *     samples?: [{ t, bitrate_kbps, fps, latency_ms, loss_pct }] }
 *
 * Usage (inside setup): `const live = useLiveSession()` → `live.current`, `live.active`, `live.state`.
 */
import { computed, getCurrentInstance, onBeforeUnmount, onMounted, reactive } from 'vue'
import { fetchJson } from './api'

export const POLL_MS = 2000
const RETRY_UNAVAILABLE_MS = 60000

export const liveState = reactive({
  /** null until the first answer; false when the host has no sessions API. */
  available: null,
  sessions: [],
  error: null,
  updatedAt: 0,
})

let subscribers = 0
let timer = null
let inflight = null

function schedule(ms) {
  clearTimeout(timer)
  timer = subscribers > 0 ? setTimeout(tick, ms) : null
}

async function tick() {
  if (typeof document !== 'undefined' && document.hidden) {
    schedule(POLL_MS)
    return
  }
  await refreshLive()
  schedule(liveState.available === false ? RETRY_UNAVAILABLE_MS : POLL_MS)
}

/**
 * Fetch sessions now (also used by pages after "End stream").
 *
 * @returns {Promise<void>}
 */
export function refreshLive() {
  if (inflight) return inflight
  inflight = (async () => {
    try {
      const data = await fetchJson('./api/sessions?samples=1')
      liveState.available = true
      liveState.sessions = Array.isArray(data?.sessions) ? data.sessions : []
      liveState.error = null
      liveState.updatedAt = Date.now()
    } catch (e) {
      if (e?.status === 404) {
        liveState.available = false
        liveState.sessions = []
        liveState.error = null
      } else {
        liveState.error = e
      }
    } finally {
      inflight = null
    }
  })()
  return inflight
}

function onVisibility() {
  if (!document.hidden && subscribers > 0) schedule(0)
}

function subscribe() {
  subscribers += 1
  if (subscribers === 1) {
    document.addEventListener('visibilitychange', onVisibility)
    schedule(0)
  }
}

function unsubscribe() {
  subscribers = Math.max(0, subscribers - 1)
  if (subscribers === 0) {
    clearTimeout(timer)
    timer = null
    document.removeEventListener('visibilitychange', onVisibility)
  }
}

/**
 * Format a session's headline numbers ("60 fps · 30 Mbps · 6.0 ms").
 *
 * @param {object} session A session from /api/sessions.
 * @returns {string}
 */
export function sessionSummary(session) {
  if (!session) return ''
  const parts = []
  if (Number.isFinite(session.fps_actual)) parts.push(`${Math.round(session.fps_actual)} fps`)
  if (Number.isFinite(session.bitrate_kbps)) parts.push(`${Math.round(session.bitrate_kbps / 1000)} Mbps`)
  if (Number.isFinite(session.latency_ms?.total)) parts.push(`${session.latency_ms.total.toFixed(1)} ms`)
  return parts.join(' · ')
}

/**
 * Bitrate series in Mbps from a session's samples (for NvSparkline).
 *
 * @param {object} session A session from /api/sessions.
 * @returns {number[]}
 */
export function bitrateSeries(session) {
  return (session?.samples || []).map((s) => (Number.isFinite(s.bitrate_kbps) ? s.bitrate_kbps / 1000 : NaN))
}

/**
 * Subscribe the calling component to live sessions for its lifetime.
 *
 * @returns {{ state: object, sessions: import('vue').ComputedRef<object[]>,
 *   current: import('vue').ComputedRef<object|null>, active: import('vue').ComputedRef<boolean>,
 *   refresh: () => Promise<void> }}
 */
export function useLiveSession() {
  if (getCurrentInstance()) {
    onMounted(subscribe)
    onBeforeUnmount(unsubscribe)
  }
  return {
    state: liveState,
    sessions: computed(() => liveState.sessions),
    current: computed(() => liveState.sessions[0] ?? null),
    active: computed(() => liveState.sessions.length > 0),
    refresh: refreshLive,
  }
}

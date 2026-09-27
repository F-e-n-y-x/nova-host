/**
 * @file Desktop preview poller shared by the preview card and the live bar thumbnail.
 *
 * GET /api/preview?display=&w= returns a JPEG (no-store). The host allows two requests a second
 * and caches for ~1 s, so one shared poller refreshes every 2 s while the tab is visible and at
 * least one consumer is on screen. 404 = the host has no preview API (`available` false);
 * 503 = capture unavailable right now (`unavailable` with the host's message); 429 = skip a beat.
 */
import { onBeforeUnmount, onMounted, reactive } from 'vue'
import { apiFetch } from '../../../fetch_utils'

export const PREVIEW_MS = 2000
const WIDTH = 960

export const previewState = reactive({
  /** null until the first answer; false when the host has no preview API. */
  available: null,
  url: '',
  display: '',
  loading: false,
  unavailable: '',
  updatedAt: 0,
})

let visibleConsumers = 0
let timer = null
let inflight = null

function schedule(ms) {
  clearTimeout(timer)
  timer = visibleConsumers > 0 && previewState.available !== false ? setTimeout(tick, ms) : null
}

async function tick() {
  if (typeof document !== 'undefined' && document.hidden) {
    schedule(PREVIEW_MS)
    return
  }
  await refreshPreview()
  schedule(PREVIEW_MS)
}

/**
 * Fetch a fresh frame now.
 *
 * @returns {Promise<void>}
 */
export function refreshPreview() {
  if (inflight) return inflight
  inflight = (async () => {
    previewState.loading = !previewState.url
    try {
      const query = new URLSearchParams({ w: String(WIDTH) })
      if (previewState.display) query.set('display', previewState.display)
      const response = await apiFetch(`./api/preview?${query}`, { cache: 'no-store' })
      if (response.status === 404) {
        previewState.available = false
        return
      }
      previewState.available = true
      if (response.status === 429) return
      if (!response.ok) {
        let message = ''
        try { message = (await response.json())?.error || '' } catch { /* not JSON */ }
        previewState.unavailable = message || String(response.status)
        return
      }
      const blob = await response.blob()
      const url = URL.createObjectURL(blob)
      if (previewState.url) URL.revokeObjectURL(previewState.url)
      previewState.url = url
      previewState.unavailable = ''
      previewState.updatedAt = Date.now()
    } catch {
      previewState.unavailable = previewState.unavailable || 'network'
    } finally {
      previewState.loading = false
      inflight = null
    }
  })()
  return inflight
}

/**
 * Switch the previewed display and fetch it right away.
 *
 * @param {string} name Display name from /api/displays.
 */
export function selectPreviewDisplay(name) {
  if (previewState.display === name) return
  previewState.display = name
  refreshPreview()
}

/**
 * Register a consumer that is currently on screen (call `release` when it scrolls away/unmounts).
 *
 * @returns {() => void} Release function.
 */
export function watchPreview() {
  visibleConsumers += 1
  if (visibleConsumers === 1) schedule(0)
  let released = false
  return () => {
    if (released) return
    released = true
    visibleConsumers = Math.max(0, visibleConsumers - 1)
    if (visibleConsumers === 0) {
      clearTimeout(timer)
      timer = null
    }
  }
}

/**
 * Poll the preview while `el` is on screen (IntersectionObserver; always-on without it).
 *
 * @param {import('vue').Ref<HTMLElement|object|null>} el Element (or component instance) to watch.
 */
export function usePreviewWhileVisible(el) {
  let release = null
  let observer = null
  const start = () => { if (!release) release = watchPreview() }
  const stop = () => { if (release) { release(); release = null } }
  onMounted(() => {
    const target = el.value?.$el ?? el.value
    if (typeof IntersectionObserver === 'undefined' || !(target instanceof Element)) {
      start()
      return
    }
    observer = new IntersectionObserver((entries) => {
      if (entries.some((e) => e.isIntersecting)) start()
      else stop()
    })
    observer.observe(target)
  })
  onBeforeUnmount(() => {
    observer?.disconnect()
    stop()
  })
}

/**
 * Test helper: forget all state.
 */
export function resetPreviewForTests() {
  clearTimeout(timer)
  timer = null
  inflight = null
  visibleConsumers = 0
  if (previewState.url && typeof URL.revokeObjectURL === 'function') URL.revokeObjectURL(previewState.url)
  Object.assign(previewState, { available: null, url: '', display: '', loading: false, unavailable: '', updatedAt: 0 })
}

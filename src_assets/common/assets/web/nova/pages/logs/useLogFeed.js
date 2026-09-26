/**
 * @file Loads the host log (/api/logs) and, while "live" is on and the tab is visible,
 * polls it. Polling pauses while the tab is hidden and catches up when it returns.
 */
import { onBeforeUnmount, onMounted, readonly, shallowRef, watch } from 'vue'

/** Milliseconds between polls while live. */
export const POLL_INTERVAL_MS = 3000

/**
 * Host log feed.
 *
 * @param {object} [options] Options.
 * @param {number} [options.interval] Poll interval in ms.
 * @param {() => Promise<string>} [options.load] Loader returning the raw log text.
 * @returns {{text: import('vue').Ref<string>, loading: import('vue').Ref<boolean>,
 *   error: import('vue').Ref<Error|null>, updatedAt: import('vue').Ref<Date|null>,
 *   live: import('vue').Ref<boolean>, hidden: import('vue').Ref<boolean>, refresh: () => Promise<void>}}
 */
export function useLogFeed({ interval = POLL_INTERVAL_MS, load = defaultLoad } = {}) {
  const text = shallowRef('')
  const loading = shallowRef(true)
  const error = shallowRef(null)
  const updatedAt = shallowRef(null)
  const live = shallowRef(true)
  const hidden = shallowRef(typeof document !== 'undefined' && document.visibilityState === 'hidden')
  let timer = null
  let inFlight = null

  async function refresh() {
    if (inFlight) return inFlight
    inFlight = (async () => {
      try {
        const next = await load()
        if (next !== text.value) text.value = next
        error.value = null
        updatedAt.value = new Date()
      } catch (e) {
        error.value = e
      } finally {
        loading.value = false
        inFlight = null
      }
    })()
    return inFlight
  }

  function stop() {
    if (timer) clearInterval(timer)
    timer = null
  }

  function schedule() {
    stop()
    if (live.value && !hidden.value) timer = setInterval(refresh, interval)
  }

  function onVisibility() {
    hidden.value = document.visibilityState === 'hidden'
    if (!hidden.value && live.value) refresh()
    schedule()
  }

  watch(live, (on) => {
    if (on) refresh()
    schedule()
  })

  onMounted(() => {
    document.addEventListener('visibilitychange', onVisibility)
    refresh()
    schedule()
  })

  onBeforeUnmount(() => {
    document.removeEventListener('visibilitychange', onVisibility)
    stop()
  })

  // `live` stays writable (the viewer's switch binds to it); the rest is read-only.
  return { text: readonly(text), loading: readonly(loading), error: readonly(error),
    updatedAt: readonly(updatedAt), hidden: readonly(hidden), live, refresh }
}

async function defaultLoad() {
  const response = await fetch('./api/logs', { cache: 'no-store' })
  if (!response.ok) throw new Error(`${response.status} ${response.statusText}`.trim())
  return response.text()
}

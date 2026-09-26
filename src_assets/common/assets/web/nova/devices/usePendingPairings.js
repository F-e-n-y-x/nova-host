/**
 * @file Poll the host for devices waiting to pair.
 */
import { onBeforeUnmount, onMounted, shallowRef } from 'vue'
import { listPairingRequests } from './deviceApi'

/**
 * Pairing requests, refreshed on an interval while the page is visible.
 *
 * @param {object} [options] Options.
 * @param {number} [options.interval] Milliseconds between checks (default 2000).
 * @returns {{requests: import('vue').ShallowRef<Array>, loaded: import('vue').ShallowRef<boolean>,
 *   failed: import('vue').ShallowRef<boolean>, refresh: () => Promise<void>}}
 */
export function usePendingPairings({ interval = 2000 } = {}) {
  const requests = shallowRef([])
  const loaded = shallowRef(false)
  const failed = shallowRef(false)
  let timer = null

  /** Check now; skipped while the tab is hidden, except for the first check. */
  async function refresh() {
    if (loaded.value && typeof document !== 'undefined' && document.hidden) return
    try {
      requests.value = await listPairingRequests()
      failed.value = false
    } catch {
      failed.value = true
    } finally {
      loaded.value = true
    }
  }

  onMounted(() => {
    refresh()
    timer = setInterval(refresh, interval)
  })
  onBeforeUnmount(() => clearInterval(timer))

  return { requests, loaded, failed, refresh }
}

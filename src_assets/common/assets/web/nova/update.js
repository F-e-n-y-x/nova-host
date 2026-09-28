/**
 * @file Host-side update status (GET /api/update/status) and the confirmed install (POST /api/update/install).
 *
 * The host checks the private release repository with its stored token, so the browser never
 * talks to GitHub. `available` is true only when the host found a strictly newer nova-vX.Y.Z
 * release; errors (private repo without a token, offline, rate limit) never show a banner.
 */
import { computed, onBeforeUnmount, ref } from 'vue'
import { fetchJson, postJson } from './api'

/** Install states that are still running (the page polls while one is shown). */
export const INSTALL_BUSY = new Set(['downloading', 'verifying', 'installing', 'restarting'])

/**
 * Fetch the update status; resolves to null on hosts without the API.
 *
 * @param {boolean} [refresh] Ask the host to check GitHub now (at most once a minute).
 * @returns {Promise<object|null>} Status object or null.
 */
export async function fetchUpdateStatus(refresh = false) {
  try {
    return await fetchJson(`./api/update/status${refresh ? '?refresh=1' : ''}`)
  } catch {
    return null
  }
}

/**
 * Whether a status object describes a real, installable update.
 *
 * @param {object|null} status Status from the host.
 * @returns {boolean} True only for a newer release the host verified by semver.
 */
export function hasUpdate(status) {
  return Boolean(status?.enabled && status.update_available === true && status.latest?.tag)
}

/**
 * Reactive update status with polling while a check or install runs.
 *
 * @param {object} [options] Options.
 * @param {number} [options.interval] Poll interval in ms while busy.
 * @returns {object} status, available, busy, installing, installError, refresh(), install(tag).
 */
export function useUpdateStatus({ interval = 2000 } = {}) {
  const status = ref(null)
  const installError = ref('')
  const starting = ref(false)
  let timer = null
  let stopped = false

  const busy = computed(() => Boolean(status.value?.checking) || INSTALL_BUSY.has(status.value?.install?.state))
  const available = computed(() => hasUpdate(status.value))

  function schedule() {
    clearTimeout(timer)
    if (!stopped && busy.value) timer = setTimeout(() => load(false), interval)
  }

  async function load(refresh) {
    const next = await fetchUpdateStatus(refresh)
    if (next || !status.value) status.value = next
    schedule()
  }

  /**
   * Start the install the owner confirmed.
   *
   * @param {string} tag Release tag shown in the dialog.
   * @returns {Promise<boolean>} True when the host accepted it.
   */
  async function install(tag) {
    installError.value = ''
    starting.value = true
    try {
      await postJson('./api/update/install', { tag })
      await load(false)
      return true
    } catch (e) {
      installError.value = e?.status === 409 ? 'conflict' : 'failed'
      await load(false)
      return false
    } finally {
      starting.value = false
    }
  }

  load(false)
  onBeforeUnmount(() => {
    stopped = true
    clearTimeout(timer)
  })

  return { status, available, busy, starting, installError, refresh: () => load(true), install }
}

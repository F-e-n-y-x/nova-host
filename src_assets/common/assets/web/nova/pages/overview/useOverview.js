/**
 * @file Data for the Overview page. Newer host APIs (/api/host/info, /api/displays, /api/health,
 * /api/sessions/history) may be missing on older hosts: a 404 resolves to `null` so widgets can
 * hide or fall back to the log instead of showing an error.
 */
import { computed } from 'vue'
import { fetchJson, getConfig, useAsync } from '../../api'
import { apiFetch } from '../../../fetch_utils'
import { detectEncoders, healthChecks, parseLogs } from '../../logs'

/**
 * Fetch JSON, resolving to null when the host doesn't have the endpoint (404).
 *
 * @param {string} url Relative API URL.
 * @returns {Promise<any|null>}
 */
export async function optionalJson(url) {
  try {
    return await fetchJson(url)
  } catch (e) {
    if (e?.status === 404) return null
    throw e
  }
}

/**
 * Load everything the Overview needs.
 *
 * @returns {object} Async sources plus derived values.
 */
export function useOverview() {
  const config = useAsync(() => getConfig())
  const hostInfo = useAsync(() => optionalJson('./api/host/info'))
  const displays = useAsync(async () => (await optionalJson('./api/displays'))?.displays ?? null)
  const health = useAsync(async () => (await optionalJson('./api/health'))?.checks ?? null)
  const history = useAsync(async () => {
    const data = await optionalJson('./api/sessions/history')
    return data ? (data.sessions || []) : null
  })
  const apps = useAsync(async () => {
    const data = await fetchJson('./api/apps')
    return { apps: data.apps || [], runningIndex: Number.isInteger(data.running_index) ? data.running_index : null,
      runningName: data.running_name || null }
  })
  const clients = useAsync(async () => (await fetchJson('./api/clients/list')).named_certs || [])
  // What runs on the host, streamed or not (a disconnect never ends the app). Older hosts: null.
  const running = useAsync(() => optionalJson('./api/apps/running'))

  // The log is only a fallback (it can be several MB): load it when the host has no /api/host/info
  // or /api/health to tell us about encoders and problems.
  const logs = useAsync(async () => {
    const response = await apiFetch('./api/logs')
    if (!response.ok) throw new Error(String(response.status))
    return parseLogs(await response.text())
  }, { immediate: false })

  const needLogs = computed(() => (!hostInfo.loading.value && hostInfo.data.value === null && !hostInfo.error.value) ||
    (!health.loading.value && health.data.value === null && !health.error.value))
  let logsRequested = false
  function ensureLogs() {
    if (needLogs.value && !logsRequested) {
      logsRequested = true
      logs.reload()
    }
  }

  const logEncoders = computed(() => detectEncoders(logs.data.value || []))
  const logHealth = computed(() => healthChecks(logs.data.value || [], config.data.value || {}))

  function reloadAll() {
    config.reload()
    hostInfo.reload()
    displays.reload()
    health.reload()
    history.reload()
    apps.reload()
    clients.reload()
    running.reload()
    if (logsRequested) logs.reload()
  }

  return { config, hostInfo, displays, health, history, apps, clients, running, logs, needLogs, ensureLogs,
    logEncoders, logHealth, reloadAll }
}

/**
 * @file State and actions for the Applications page: the app list, host platform,
 * cover URLs (cache-busted after changes, with broken covers remembered), deleting an
 * app and closing the running one.
 */
import { computed, reactive, shallowRef } from 'vue'
import { fetchJson, getConfig, postJson, useAsync } from '../../api'
import { apiFetch } from '../../../fetch_utils'

/**
 * Applications page state.
 *
 * @returns {object} `apps` (useAsync result), `platform`, `total`, `coverUrl(app, index)`,
 *   `markCoverBroken(index)`, `refresh()`, `removeApp(index)`, `closeRunning()`.
 */
export function useApps() {
  const apps = useAsync(async () => (await fetchJson('./api/apps')).apps || [])
  const platform = shallowRef('')
  const coverVersion = shallowRef(0)
  const brokenCovers = reactive(new Set())

  getConfig().then((c) => { platform.value = c?.platform || '' }).catch(() => {})

  const total = computed(() => apps.data.value?.length ?? 0)

  function coverUrl(app, index) {
    if (!app['image-path'] || brokenCovers.has(index)) return ''
    return `./api/covers/${index}?v=${coverVersion.value}`
  }

  function markCoverBroken(index) {
    brokenCovers.add(index)
  }

  async function refresh() {
    brokenCovers.clear()
    coverVersion.value += 1
    await apps.reload()
  }

  async function removeApp(index) {
    const response = await apiFetch(`./api/apps/${index}`, { method: 'DELETE', headers: { 'Content-Type': 'application/json' } })
    if (!response.ok) throw new Error(String(response.status))
    await refresh()
  }

  function closeRunning() {
    return postJson('./api/apps/close')
  }

  return { apps, platform, total, coverUrl, markCoverBroken, refresh, removeApp, closeRunning }
}

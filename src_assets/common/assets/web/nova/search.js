/**
 * @file Global search (the top bar field and the Ctrl/⌘K command palette).
 *
 * Providers return result items for a query. Built-ins cover pages, settings, applications and
 * devices; a page can add its own for its lifetime with `useSearchProvider(provider)`.
 *
 * Provider: { id, group (translated heading), order?, search(query, { t }) → items | Promise<items> }
 * Item:     { id, label, sub?, icon? (lucide component), to? (route location), run? (function),
 *             keywords? (extra match text) }
 */
import { getCurrentInstance, onBeforeUnmount, reactive, shallowRef } from 'vue'
import { Activity, Gamepad2, Link2, LayoutDashboard, SlidersHorizontal, Smartphone, CircleHelp, KeyRound } from '@lucide/vue'
import { OPTIONS, SECTIONS } from '../configs/settings_schema'
import { fetchJson } from './api'

const providers = reactive(new Map())

export const palette = reactive({ open: false, query: '' })

/**
 * Open the command palette, optionally pre-filled.
 *
 * @param {string} [query] Initial query.
 */
export function openPalette(query = '') {
  palette.query = query
  palette.open = true
}

/** Close the command palette. */
export function closePalette() {
  palette.open = false
}

/**
 * Register a provider. Returns a function that removes it.
 *
 * @param {object} provider See file header.
 * @returns {() => void}
 */
export function registerSearchProvider(provider) {
  providers.set(provider.id, provider)
  return () => providers.delete(provider.id)
}

/**
 * Register a provider for the calling component's lifetime.
 *
 * @param {object} provider See file header.
 */
export function useSearchProvider(provider) {
  const remove = registerSearchProvider(provider)
  if (getCurrentInstance()) onBeforeUnmount(remove)
}

/**
 * Case-insensitive match of every query word against the item's text.
 *
 * @param {string} query User query.
 * @param {...string} fields Text to match.
 * @returns {boolean}
 */
export function matches(query, ...fields) {
  const hay = fields.filter(Boolean).join(' ').toLowerCase()
  return query.toLowerCase().split(/\s+/).filter(Boolean).every((word) => hay.includes(word))
}

// ---------------------------------------------------------------- built-ins

const PAGES = [
  { id: 'overview', key: 'nova.nav.overview', to: '/', icon: LayoutDashboard },
  { id: 'library', key: 'nova.nav.library', to: '/library', icon: Gamepad2 },
  { id: 'devices', key: 'nova.nav.devices', to: '/devices', icon: Smartphone },
  { id: 'pair', key: 'nova.nav.pair', to: '/pair', icon: Link2 },
  { id: 'settings', key: 'nova.nav.settings', to: '/settings', icon: SlidersHorizontal },
  { id: 'logs', key: 'nova.nav.logs', to: '/logs', icon: Activity },
  { id: 'help', key: 'nova.nav.help', to: '/help', icon: CircleHelp },
  { id: 'password', key: 'nova.nav.password', to: '/password', icon: KeyRound },
]

registerSearchProvider({
  id: 'pages',
  order: 0,
  groupKey: 'nova.search.group_pages',
  search: (query, { t }) => PAGES.filter((p) => !query || matches(query, t(p.key), p.id))
    .map((p) => ({ id: `page:${p.id}`, label: t(p.key), icon: p.icon, to: p.to })),
})

const SECTION_OF = new Map(SECTIONS.flatMap((s) => s.options.map((key) => [key, s.id])))

registerSearchProvider({
  id: 'settings',
  order: 1,
  groupKey: 'nova.search.group_settings',
  search: (query, { t, te }) => {
    if (!query.trim()) return []
    return Object.keys(OPTIONS)
      .map((key) => {
        const label = te(`config.${key}`) ? t(`config.${key}`) : key
        const section = SECTION_OF.get(key)
        const sub = section && te(`nova.settings.sections.${section}.title`) ? t(`nova.settings.sections.${section}.title`) : ''
        return { id: `setting:${key}`, label, sub, keywords: key, icon: SlidersHorizontal, to: { path: '/settings', hash: `#${key}` } }
      })
      .filter((item) => matches(query, item.label, item.keywords, item.sub))
      .slice(0, 8)
  },
})

// Apps and devices are fetched once per palette session and cached briefly.
const cache = { apps: shallowRef(null), devices: shallowRef(null), at: 0 }

async function cached(kind, url, pick) {
  if (!cache[kind].value || Date.now() - cache.at > 30000) {
    try {
      cache[kind].value = pick(await fetchJson(url))
      cache.at = Date.now()
    } catch {
      cache[kind].value = cache[kind].value || []
    }
  }
  return cache[kind].value
}

registerSearchProvider({
  id: 'apps',
  order: 2,
  groupKey: 'nova.search.group_apps',
  search: async (query) => {
    if (!query.trim()) return []
    const apps = await cached('apps', './api/apps', (d) => d?.apps || [])
    return apps.map((app, index) => ({ app, index }))
      .filter(({ app }) => matches(query, app.name, app.cmd))
      .slice(0, 6)
      .map(({ app, index }) => ({ id: `app:${index}`, label: app.name, sub: app.cmd || '', icon: Gamepad2,
        to: { path: '/library', query: { edit: String(index) } } }))
  },
})

registerSearchProvider({
  id: 'devices',
  order: 3,
  groupKey: 'nova.search.group_devices',
  search: async (query) => {
    if (!query.trim()) return []
    const devices = await cached('devices', './api/clients/list', (d) => d?.named_certs || [])
    return devices.filter((d) => matches(query, d.name, d.uuid)).slice(0, 6)
      .map((d) => ({ id: `device:${d.uuid}`, label: d.name || d.uuid, icon: Smartphone, to: `/devices/${d.uuid}` }))
  },
})

/**
 * Run every provider for a query and group the results in provider order.
 *
 * @param {string} query User query.
 * @param {{t: Function, te: Function}} i18n vue-i18n helpers.
 * @returns {Promise<Array<{id: string, group: string, items: object[]}>>}
 */
export async function runSearch(query, i18n) {
  const list = [...providers.values()].sort((a, b) => (a.order ?? 9) - (b.order ?? 9))
  const results = await Promise.all(list.map(async (p) => {
    try {
      const items = await p.search(query, i18n)
      return { id: p.id, group: p.group || (p.groupKey ? i18n.t(p.groupKey) : p.id), items: items || [] }
    } catch {
      return { id: p.id, group: p.group || p.id, items: [] }
    }
  }))
  return results.filter((g) => g.items.length > 0)
}

/** Forget cached apps/devices (tests, or after edits). */
export function clearSearchCache() {
  cache.apps.value = null
  cache.devices.value = null
  cache.at = 0
}

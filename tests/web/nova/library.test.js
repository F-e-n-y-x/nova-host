import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import { createI18n } from 'vue-i18n'
import { createMemoryHistory, createRouter } from 'vue-router'

import en from '../../../src_assets/common/assets/web/public/assets/locale/en.json'
import Apps from '../../../src_assets/common/assets/web/Apps.vue'
import AddGamesSheet from '../../../src_assets/common/assets/web/nova/pages/library/AddGamesSheet.vue'
import {
  appKind, appRunner, artUrl, importPayload, initialReview, jobFraction, matchLevel, pollJob, probeLibraryApi,
} from '../../../src_assets/common/assets/web/nova/pages/library/libraryApi.js'
import { toasts } from '../../../src_assets/common/assets/web/nova/toast.js'
import { fitSize } from '../../../src_assets/common/assets/web/nova/pages/library/artworkUpload.js'

const APPS = [
  { name: 'Desktop' },
  { name: 'Steam', cmd: 'steam' },
  { name: 'Far Cry 5', cmd: '/usr/local/bin/run-windows-exe "/g/fc5.exe"', 'nova-source': 'folder', 'image-path': '/c/p.png' },
]

const ITEMS = [
  { temp_id: 'j:0', title: 'Far Cry 5', source: 'folder', working_dir: '/g/fc5', launch_cmd: 'x', already_in_library: false,
    matched: { appid: 552520, name: 'Far Cry 5', confidence: 1 },
    artwork: { poster: [{ id: 'p1', label: 'Steam' }, { id: 'p2', label: 'SGDB' }], hero: [{ id: 'h1' }], logo: [], icon: [] } },
  { temp_id: 'j:1', title: 'Immortals', source: 'folder', working_dir: '/g/imm', launch_cmd: 'y', already_in_library: false,
    matched: { appid: 1, name: 'Immortals Fenyx Rising', confidence: 0.6 }, artwork: {} },
  { temp_id: 'j:2', title: 'GTA V', source: 'lutris', launch_cmd: 'lutris:rungameid/1', already_in_library: true, matched: null, artwork: {} },
]

function json(body, { status = 200, type = 'application/json' } = {}) {
  return {
    ok: status < 400, status, statusText: status < 400 ? 'OK' : 'Error',
    headers: { get: (h) => (h.toLowerCase() === 'content-type' ? type : null) },
    json: async () => body,
    clone() { return this },
  }
}

let wrapper
let calls

function stubHost({ library = true, apps = APPS, running = null } = {}) {
  calls = []
  vi.stubGlobal('fetch', vi.fn(async (url, options = {}) => {
    const method = options.method || 'GET'
    calls.push({ url, method, body: options.body ? JSON.parse(options.body) : undefined })
    if (url.startsWith('./api/library/')) {
      if (!library) return json('<html>', { type: 'text/html' })
      if (url.startsWith('./api/library/artwork/search')) return json({ matches: [], artwork: {} })
      if (url === './api/library/scan') return json({ job_id: 'scan1' })
      if (url === './api/library/import') return json({ job_id: 'imp1' })
      if (url === './api/library/jobs/scan1') return json({ state: 'done', progress: { done: 3, total: 3 }, result: { items: ITEMS, skipped: [] } })
      if (url === './api/library/jobs/imp1') return json({ state: 'done', result: { imported: [1, 2], duplicates: [], failed: [] } })
    }
    if (url === './api/apps' && method === 'GET') return json({ apps, running_index: running, running_name: null })
    if (url === './api/config') return json({ platform: 'linux' })
    return json({ status: true })
  }))
}

async function mountAt(path, opts = {}) {
  stubHost(opts)
  const router = createRouter({
    history: createMemoryHistory(),
    routes: [{ path: '/library', component: Apps }, { path: '/', component: { template: '<div />' } }],
  })
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en } })
  await router.push(path)
  await router.isReady()
  wrapper = mount({ template: '<router-view />' }, { attachTo: document.body, global: { plugins: [router, i18n] } })
  await flushPromises()
  return router
}

beforeEach(() => {
  try { localStorage.clear() } catch { /* jsdom without storage */ }
})
afterEach(() => {
  wrapper?.unmount()
  wrapper = null
  document.body.innerHTML = ''
  vi.unstubAllGlobals()
})

describe('library helpers', () => {
  it('classifies apps and runners', () => {
    expect(appKind({ name: 'Desktop' })).toBe('desktop')
    expect(appKind({ cmd: 'firefox' })).toBe('app')
    expect(appKind({ cmd: 'steam steam://rungameid/1' })).toBe('game')
    expect(appKind({ cmd: 'x', 'nova-source': 'lutris' })).toBe('game')
    expect(appRunner({ cmd: '/usr/local/bin/run-windows-exe a.exe' })).toBe('runner_proton')
    expect(appRunner({ cmd: 'lutris lutris:rungameid/4' })).toBe('runner_lutris')
    expect(appRunner({ cmd: 'firefox' })).toBe('')
  })

  it('builds stored artwork URLs only for artwork the app has', () => {
    expect(artUrl({ 'image-path': '/p.png' }, 3, 'poster', 2)).toBe('./api/covers/3?v=2')
    expect(artUrl({ 'nova-hero': '/h.jpg' }, 3, 'hero')).toBe('./api/covers/3/hero?v=0')
    expect(artUrl({}, 3, 'icon')).toBe('')
  })

  it('reads job progress and match levels', () => {
    expect(jobFraction({ progress: { done: 1, total: 4 } })).toBe(0.25)
    expect(jobFraction({ progress: { done: 0, total: 0 } })).toBeNull()
    expect(matchLevel(ITEMS[0])).toBe('sure')
    expect(matchLevel(ITEMS[1])).toBe('check')
    expect(matchLevel(ITEMS[2])).toBe('none')
  })

  it('starts duplicates unselected and sends the chosen artwork per kind', () => {
    const review = initialReview(ITEMS)
    expect(review['j:2'].selected).toBe(false)
    review['j:0'].art.poster = 'p2'
    review['j:1'].title = 'Immortals Fenyx Rising'
    expect(importPayload(ITEMS, review)).toEqual([
      { temp_id: 'j:0', poster: 'p2', hero: 'h1', logo: 'none', icon: 'none' },
      { temp_id: 'j:1', title: 'Immortals Fenyx Rising', poster: 'none', hero: 'none', logo: 'none', icon: 'none' },
    ])
  })

  it('polls a job until it is done, and rejects with the host error', async () => {
    const seen = []
    const states = [{ state: 'running' }, { state: 'running' }, { state: 'done', result: { items: [] } }]
    const job = await pollJob('x', { interval: 0, fetchJob: async () => states.shift(), onUpdate: (j) => seen.push(j.state) })
    expect(job.state).toBe('done')
    expect(seen).toEqual(['running', 'running', 'done'])
    await expect(pollJob('y', { interval: 0, fetchJob: async () => ({ state: 'failed', error: 'Steam is not installed' }) }))
      .rejects.toThrow('Steam is not installed')
  })

  it('treats an HTML answer or a 404 as "no library API"', async () => {
    expect(await probeLibraryApi(async () => json({}, { status: 400 }))).toBe(true)
    expect(await probeLibraryApi(async () => json('<html>', { type: 'text/html' }))).toBe(false)
    expect(await probeLibraryApi(async () => json({}, { status: 404 }))).toBe(false)
  })

  it('fits posters inside 600×900 without enlarging them', () => {
    expect(fitSize(1200, 1800)).toEqual({ w: 600, h: 900 })
    expect(fitSize(300, 450)).toEqual({ w: 300, h: 450 })
  })
})

describe('Library page', () => {
  it('shows Add games as the one primary action when the host has the library API', async () => {
    await mountAt('/library')
    const primary = document.querySelector('.nv-lib-add')
    expect(primary.textContent).toContain('Add games')
  })

  it('falls back to "Add application manually" on hosts without the library API', async () => {
    await mountAt('/library', { library: false })
    expect(document.querySelector('.nv-lib-add').textContent).toContain('Add application manually')
    document.querySelector('.nv-lib-add').click()
    await flushPromises()
    expect(document.getElementById('nv-app-name')).not.toBeNull()
  })

  it('filters by kind and search from the URL, with a filtered empty state', async () => {
    const router = await mountAt('/library?kind=game&view=list')
    expect(document.body.textContent).toContain('Far Cry 5')
    expect(document.body.textContent).not.toContain('Steam Big')
    await router.replace('/library?q=zzz')
    await flushPromises()
    expect(document.body.textContent).toContain('No matches for “zzz”')
    ;[...document.querySelectorAll('button')].find((b) => b.textContent.includes('Clear filters')).click()
    await flushPromises()
    expect(router.currentRoute.value.query.q).toBeUndefined()
  })

  it('marks the running app from running_index', async () => {
    await mountAt('/library?view=list', { running: 1 })
    const rows = [...document.querySelectorAll('tbody tr')]
    expect(rows[1].textContent).toContain('Running')
    expect(rows[0].textContent).not.toContain('Running')
  })

  it('opens the editor with the hero and deletes through a named confirm, then offers Undo', async () => {
    const router = await mountAt('/library?view=list')
    document.getElementById('nv-app-2').click()
    await flushPromises()
    expect(router.currentRoute.value.query.edit).toBe('2')
    expect(document.querySelector('.nv-editor__hero')).not.toBeNull()
    expect(document.getElementById('nv-app-name').value).toBe('Far Cry 5')
    router.back()
    await flushPromises()
    await new Promise((r) => setTimeout(r, 0))
    await flushPromises()
    document.querySelectorAll('tbody [aria-haspopup="menu"]')[2].click()
    await flushPromises()
    ;[...document.querySelectorAll('[role="menuitem"]')].find((b) => b.textContent.includes('Delete')).click()
    await flushPromises()
    expect(document.body.textContent).toContain('Delete Far Cry 5?')
    expect(document.activeElement.textContent.trim()).toBe('Cancel')
    ;[...document.querySelectorAll('[role="dialog"] button')].find((b) => b.textContent.trim() === 'Delete').click()
    await flushPromises()
    expect(calls.some((c) => c.url === './api/apps/2' && c.method === 'DELETE')).toBe(true)
    expect(toasts.some((t) => t.action?.label === 'Undo' && t.message.includes('Far Cry 5'))).toBe(true)
  })
})

describe('Add games sheet', () => {
  function mountSheet() {
    stubHost()
    const router = createRouter({ history: createMemoryHistory(), routes: [{ path: '/:p(.*)*', component: { template: '<div />' } }] })
    const i18n = createI18n({ legacy: false, locale: 'en', messages: { en } })
    wrapper = mount(AddGamesSheet, { props: { open: false }, attachTo: document.body, global: { plugins: [router, i18n] } })
    return wrapper
  }

  it('asks for a full path before scanning a folder', async () => {
    const w = mountSheet()
    await w.setProps({ open: true })
    ;[...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'Scan').click()
    await flushPromises()
    expect(document.body.textContent).toContain('Enter the full path of a folder')
    expect(calls.some((c) => c.url === './api/library/scan')).toBe(false)
  })

  it('scans, reviews with confidence and duplicates, and imports the selection', async () => {
    const w = mountSheet()
    await w.setProps({ open: true })
    const input = document.querySelector('#nv-addgames-folder, .nv-addgames__folder input')
    input.value = '/DATA/Games'
    input.dispatchEvent(new Event('input'))
    await flushPromises()
    ;[...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'Scan').click()
    await flushPromises()
    await new Promise((r) => setTimeout(r, 0))
    await flushPromises()
    expect(calls.find((c) => c.url === './api/library/scan').body).toEqual({ source: 'folder', path: '/DATA/Games' })
    expect(document.body.textContent).toContain('Review 3 games')
    expect(document.body.textContent).toContain('Sure match')
    expect(document.body.textContent).toContain('Check this match — Immortals Fenyx Rising?')
    expect(document.body.textContent).toContain('Already in your library')
    const add = [...document.querySelectorAll('button')].find((b) => b.textContent.includes('Add 2 games'))
    add.click()
    await flushPromises()
    await new Promise((r) => setTimeout(r, 0))
    await flushPromises()
    const sent = calls.find((c) => c.url === './api/library/import').body.items
    expect(sent.map((i) => i.temp_id)).toEqual(['j:0', 'j:1'])
    expect(w.emitted('imported')[0][0]).toEqual({ imported: 2, duplicates: 0, failed: 0 })
  })

  it('marks a source unavailable when the host says it isn’t installed', async () => {
    const w = mountSheet()
    const base = globalThis.fetch
    vi.stubGlobal('fetch', vi.fn(async (url, options) => (url === './api/library/jobs/scan1'
      ? json({ state: 'failed', error: 'Steam is not installed on this host' })
      : base(url, options))))
    await w.setProps({ open: true })
    const steamRow = [...document.querySelectorAll('.nv-addgames__source')].find((li) => li.textContent.includes('Import from Steam'))
    steamRow.querySelector('button').click()
    await flushPromises()
    await new Promise((r) => setTimeout(r, 0))
    await flushPromises()
    expect(document.body.textContent).toContain('Steam isn’t installed on this PC.')
  })
})

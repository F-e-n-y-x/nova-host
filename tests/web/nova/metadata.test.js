import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { nextTick } from 'vue'
import { flushPromises } from '@vue/test-utils'

import { mountNova } from './helpers.js'
import ArtSourcesEditor from '../../../src_assets/common/assets/web/configs/components/ArtSourcesEditor.vue'
import LibraryMetadataPanel from '../../../src_assets/common/assets/web/configs/components/LibraryMetadataPanel.vue'
import SettingsSection from '../../../src_assets/common/assets/web/configs/components/SettingsSection.vue'
import AppMetadataPanel from '../../../src_assets/common/assets/web/nova/pages/apps/AppMetadataPanel.vue'
import AppArtworkPanel from '../../../src_assets/common/assets/web/nova/pages/apps/AppArtworkPanel.vue'
import MatchResults from '../../../src_assets/common/assets/web/nova/pages/library/MatchResults.vue'
import { artworkSites } from '../../../src_assets/common/assets/web/nova/pages/library/libraryApi.js'
import AppEditor from '../../../src_assets/common/assets/web/nova/pages/apps/AppEditor.vue'
import { OPTIONS, SECTIONS, parseArtSources } from '../../../src_assets/common/assets/web/configs/settings_schema.js'
import { optionApplies } from '../../../src_assets/common/assets/web/configs/settings_model.js'
import {
  hasOverride, matchCandidates, matchChange, relativeTime,
} from '../../../src_assets/common/assets/web/nova/pages/library/metadataApi.js'

const wrappers = []
function track(w) {
  wrappers.push(w)
  return w
}

/**
 * Stub fetch with handlers keyed by "METHOD path" (query string included).
 *
 * @param {Record<string, object|((body: object) => object)>} routes Replies.
 * @returns {import('vitest').Mock} The fetch mock.
 */
function routeFetch(routes) {
  const mock = vi.fn(async (url, options = {}) => {
    const key = `${options.method || 'GET'} ${url}`
    const handler = routes[key]
    if (handler === undefined) {
      return { ok: false, status: 404, statusText: 'Not Found', json: async () => ({}), clone() { return this } }
    }
    const body = options.body ? JSON.parse(options.body) : undefined
    const reply = typeof handler === 'function' ? handler(body) : handler
    return { ok: true, status: 200, statusText: 'OK', json: async () => reply, clone() { return this } }
  })
  vi.stubGlobal('fetch', mock)
  return mock
}

const calls = (mock, key) => mock.mock.calls.filter(([url, o = {}]) => `${o.method || 'GET'} ${url}` === key)
const buttonByText = (text) => [...document.body.querySelectorAll('button')].find((b) => b.textContent.trim() === text)

afterEach(() => {
  wrappers.splice(0).forEach((w) => w.unmount())
  document.body.innerHTML = ''
  vi.unstubAllGlobals()
  vi.restoreAllMocks()
})

describe('Library & artwork settings schema', () => {
  it('has its own section with every metadata and artwork option', () => {
    const section = SECTIONS.find((s) => s.id === 'library')
    expect(section.panel).toBe('LibraryMetadataPanel')
    expect(section.options).toEqual(expect.arrayContaining([
      'metadata_steam', 'metadata_language', 'metadata_auto_fetch', 'metadata_ttl_days', 'art_source_priority',
      'art_prefer_official', 'art_steamgriddb_poster_style', 'art_steamgriddb_hero_style', 'art_allow_animated',
      'art_nsfw', 'art_humor', 'steamgriddb_api_key', 'igdb_client_id', 'igdb_client_secret', 'rawg_api_key',
    ]))
    expect(SECTIONS.find((s) => s.id === 'general').options).not.toContain('steamgriddb_api_key')
    expect(OPTIONS.metadata_ttl_days).toMatchObject({ type: 'number', min: 1, max: 365 })
    for (const key of ['steamgriddb_api_key', 'igdb_client_secret', 'rawg_api_key']) expect(OPTIONS[key].type).toBe('secret')
  })

  it('shows SteamGridDB style options only while SteamGridDB is a source', () => {
    const config = { art_source_priority: 'steam,steamgriddb', metadata_steam: 'enabled' }
    expect(optionApplies('art_steamgriddb_poster_style', config, 'linux')).toBe(true)
    config.art_source_priority = 'steam,lutris'
    expect(optionApplies('art_steamgriddb_poster_style', config, 'linux')).toBe(false)
    expect(optionApplies('art_nsfw', config, 'linux')).toBe(false)
    config.metadata_steam = 'disabled'
    expect(optionApplies('metadata_language', config, 'linux')).toBe(false)
  })

  it('parses source lists like the host (trimmed, lower-case, known, no repeats)', () => {
    expect(parseArtSources(' SteamGridDB, igdb,nonsense,steam,igdb')).toEqual(['steamgriddb', 'igdb', 'steam'])
    expect(parseArtSources('')).toEqual([])
  })
})

describe('ArtSourcesEditor', () => {
  const rows = (w) => w.findAll('.nv-art-sources__item').map((r) => r.attributes('data-source'))
  /** Mount with v-model wired up, like SettingField does. */
  function mountEditor(modelValue, config = {}) {
    const w = track(mountNova(ArtSourcesEditor, {
      props: { modelValue, config, 'onUpdate:modelValue': (v) => w.setProps({ modelValue: v }) },
    }))
    return w
  }

  it('lists sources in use first and reorders them', async () => {
    const w = mountEditor('steamgriddb,steam')
    expect(rows(w)).toEqual(['steamgriddb', 'steam', 'lutris', 'igdb'])
    expect(w.find('button[aria-label="Move SteamGridDB up"]').element.disabled).toBe(true)
    expect(w.find('button[aria-label="Move Lutris up"]').element.disabled).toBe(true)  // off: not ranked
    await w.find('button[aria-label="Move Steam up"]').trigger('click')
    expect(w.emitted('update:modelValue').at(-1)).toEqual(['steam,steamgriddb'])
    expect(rows(w)).toEqual(['steam', 'steamgriddb', 'lutris', 'igdb'])
  })

  it('turns sources on (at the end) and off, and says when a key is missing', async () => {
    const w = mountEditor('steam,steamgriddb', { steamgriddb_api_key: '' })
    expect(w.find('[data-source="steamgriddb"]').text()).toContain('needs a key')
    await w.find('[data-source="igdb"] [role="switch"]').trigger('click')
    expect(w.emitted('update:modelValue').at(-1)).toEqual(['steam,steamgriddb,igdb'])
    await w.find('[data-source="steam"] [role="switch"]').trigger('click')
    expect(w.emitted('update:modelValue').at(-1)).toEqual(['steamgriddb,igdb'])
  })
})

describe('LibraryMetadataPanel', () => {
  beforeEach(() => vi.spyOn(Date, 'now').mockReturnValue(1_700_000_000_000))

  it('shows how many games have details and when the library was refreshed', async () => {
    routeFetch({ 'GET ./api/library/metadata/status': { total: 12, matched: 9, last_refresh_at: 1_700_000_000 - 3 * 86400 } })
    const w = track(mountNova(LibraryMetadataPanel))
    await flushPromises()
    expect(w.text()).toContain('9 of 12 games have details.')
    expect(w.text()).toContain('Last refresh 3 days ago')
  })

  it('refreshes every game as a job and updates the status', async () => {
    let matched = 1
    const fetch = routeFetch({
      'GET ./api/library/metadata/status': () => ({ total: 2, matched, last_refresh_at: null }),
      'POST ./api/library/metadata/refresh': () => { matched = 2; return { job_id: 'j1' } },
      'GET ./api/library/jobs/j1': { state: 'done', progress: { done: 2, total: 2 }, result: { refreshed: 2, matched: 2 } },
    })
    const w = track(mountNova(LibraryMetadataPanel))
    await flushPromises()
    expect(w.text()).toContain('Not refreshed yet')
    await buttonByText('Refresh all now').click()
    await flushPromises()
    expect(JSON.parse(calls(fetch, 'POST ./api/library/metadata/refresh')[0][1].body)).toEqual({ all: true })
    expect(w.text()).toContain('2 of 2 games have details.')
  })

  it('clears the cache only after confirming', async () => {
    const fetch = routeFetch({
      'GET ./api/library/metadata/status': { total: 1, matched: 0, last_refresh_at: null },
      'POST ./api/library/metadata/clear-cache': { removed: 4 },
    })
    track(mountNova(LibraryMetadataPanel))
    await flushPromises()
    buttonByText('Clear cache').click()
    await flushPromises()
    expect(document.body.textContent).toContain('Clear cached game details?')
    expect(calls(fetch, 'POST ./api/library/metadata/clear-cache')).toHaveLength(0)
    const dialog = document.body.querySelector('[role="alertdialog"], [role="dialog"]')
    ;[...dialog.querySelectorAll('button')].find((b) => b.textContent.trim() === 'Clear cache').click()
    await flushPromises()
    expect(calls(fetch, 'POST ./api/library/metadata/clear-cache')).toHaveLength(1)
  })

  it('is shown at the top of the Library & artwork section', async () => {
    routeFetch({ 'GET ./api/library/metadata/status': { total: 0, matched: 0, last_refresh_at: null } })
    const section = { id: 'library', title: 'Library & artwork', summary: '', panel: 'LibraryMetadataPanel', items: [] }
    const w = track(mountNova(SettingsSection, { props: { section, config: {}, platform: 'linux' } }))
    await flushPromises()
    expect(w.find('.nv-meta-panel').exists()).toBe(true)
    expect(w.text()).toContain('No games yet.')
  })
})

describe('metadataApi helpers', () => {
  it('merges Steam and IGDB candidates, best first, and builds the change body', () => {
    const list = matchCandidates({ candidates: [
      { source: 'igdb', appid: null, igdb_id: 99, name: 'Hades II', year: '2024', confidence: 0.9 },
      { source: 'catalog', appid: 10, igdb_id: 5, name: 'Hades', confidence: 0.8 },
      { source: 'steamgriddb', appid: null, igdb_id: null, sgdb_id: 3, name: 'No ids', confidence: 0.7 },
    ] })
    expect(list.map((c) => c.key)).toEqual(['igdb-99', 'steam-10'])
    expect(matchChange(list[0])).toEqual({ igdb_id: 99, steam_appid: null, refetch: true })
    expect(matchChange(list[1])).toEqual({ steam_appid: 10, igdb_id: null, refetch: true })
    expect(hasOverride({ override: { steam_appid: null, igdb_id: 5 } })).toBe(true)
    expect(hasOverride({ override: { steam_appid: null, igdb_id: null } })).toBe(false)
  })

  it('formats relative times', () => {
    expect(relativeTime(null, 'en')).toBe('')
    expect(relativeTime(1000 - 7200, 'en', 1000 * 1000)).toBe('2 hours ago')
  })
})

describe('AppMetadataPanel', () => {
  const STATUS = {
    name: 'Hades', override: { steam_appid: null, igdb_id: null },
    match: { source: 'steam', id: 1145360, name: 'Hades', confidence: 0.93 },
    details: { source: 'steam', fetched_at: 0 },
  }

  it('shows the current match with its source, id and confidence', async () => {
    routeFetch({ 'GET ./api/library/metadata/4': STATUS })
    const w = track(mountNova(AppMetadataPanel, { props: { index: 4, name: 'Hades', draftName: 'Hades' } }))
    await flushPromises()
    const card = w.find('[data-test="meta-match"]').text()
    expect(card).toContain('Steam · 1145360')
    expect(card).toContain('93% match')
    expect(card).toContain('Details not fetched yet')
  })

  it('changes the match from a search and re-fetches the details', async () => {
    let status = { ...STATUS, match: null }
    const fetch = routeFetch({
      'GET ./api/library/metadata/4': () => status,
      'GET ./api/library/metadata/search?q=Hades': { candidates: [{ source: 'steam', appid: 1145360, name: 'Hades', year: '2020', confidence: 1 }], igdb: false, steamgriddb: false },
      'POST ./api/library/metadata/4': (body) => {
        status = { ...STATUS, override: { steam_appid: body.steam_appid, igdb_id: null } }
        return { ...status, job_id: 'm1' }
      },
      'GET ./api/library/jobs/m1': { state: 'done', result: {} },
    })
    const w = track(mountNova(AppMetadataPanel, { props: { index: 4, name: 'Hades', draftName: 'Hades' } }))
    await flushPromises()
    expect(w.text()).toContain('No match.')
    await w.findAll('button').find((b) => b.text() === 'Change match').trigger('click')
    await flushPromises()
    expect(w.text()).toContain('Add SteamGridDB or IGDB keys')  // says how to search more sources
    expect(w.text()).toContain('2020 · Steam 1145360')
    await w.findAll('button').find((b) => b.text() === 'Use').trigger('click')
    await flushPromises()
    expect(JSON.parse(calls(fetch, 'POST ./api/library/metadata/4')[0][1].body))
      .toEqual({ steam_appid: 1145360, igdb_id: null, refetch: true })
    expect(w.emitted('match-changed')).toHaveLength(1)
    expect(w.text()).toContain('Set by you')
  })

  it('resets details to automatic matching', async () => {
    const fetch = routeFetch({
      'GET ./api/library/metadata/4': STATUS,
      'POST ./api/library/metadata/4': { ...STATUS, job_id: 'r1' },
      'GET ./api/library/jobs/r1': { state: 'done', result: {} },
    })
    const w = track(mountNova(AppMetadataPanel, { props: { index: 4, name: 'Hades', draftName: 'Hades' } }))
    await flushPromises()
    await w.findAll('button').find((b) => b.text() === 'Reset details to auto').trigger('click')
    await flushPromises()
    expect(JSON.parse(calls(fetch, 'POST ./api/library/metadata/4')[0][1].body)).toEqual({ reset: true, refetch: true })
    expect(w.text()).toContain('Details updated.')
  })

  it('resets artwork to the first automatic candidate of each kind', async () => {
    const fetch = routeFetch({
      'GET ./api/library/metadata/4': STATUS,
      'GET ./api/library/artwork/search?appid=1145360': {
        matches: [], artwork: { poster: [{ id: 'p1' }, { id: 'p2' }], hero: [{ id: 'h1' }], logo: [], icon: [{ id: 'i1' }] },
      },
      'POST ./api/library/artwork/apply': { job_id: 'a1' },
      'GET ./api/library/jobs/a1': { state: 'done', result: {} },
    })
    const w = track(mountNova(AppMetadataPanel, { props: { index: 4, name: 'Hades', draftName: 'Hades' } }))
    await flushPromises()
    await w.findAll('button').find((b) => b.text() === 'Reset artwork to auto').trigger('click')
    await flushPromises()
    expect(JSON.parse(calls(fetch, 'POST ./api/library/artwork/apply')[0][1].body))
      .toEqual({ app_index: 4, poster: 'p1', hero: 'h1', icon: 'i1' })
    expect(w.emitted('artwork-applied')).toHaveLength(1)
  })
})

describe('AppEditor Metadata tab', () => {
  const GAME = { name: 'Hades', cmd: 'steam steam://rungameid/1145360', 'nova-source': 'steam', 'nova-steam-appid': 1145360 }

  it('is offered for saved apps on hosts with the library API only', async () => {
    routeFetch({})
    track(mountNova(AppEditor, { props: { open: true, app: null, index: -1, platform: 'linux', libraryApi: true } }))
    await nextTick()
    expect(buttonByText('Metadata')).toBeUndefined()
    wrappers.splice(0).forEach((w) => w.unmount())
    track(mountNova(AppEditor, { props: { open: true, app: GAME, index: 1, platform: 'linux', libraryApi: false } }))
    await nextTick()
    expect(buttonByText('Metadata')).toBeUndefined()
  })

  it('switches to the Metadata tab and keeps a changed match in the form', async () => {
    let saved = { ...GAME }
    let status = { name: 'Hades', override: { steam_appid: null, igdb_id: null }, match: null, details: null }
    const fetch = routeFetch({
      'GET ./api/library/metadata/1': () => status,
      'GET ./api/library/metadata/search?q=Hades': { candidates: [{ source: 'igdb', appid: null, igdb_id: 7, name: 'Hades', year: '2020', confidence: 1 }], igdb: true, steamgriddb: true },
      'POST ./api/library/metadata/1': () => {
        saved = { ...GAME, 'nova-igdb-id': 7 }
        status = { name: 'Hades', override: { steam_appid: null, igdb_id: 7 }, match: { source: 'igdb', id: 7, name: 'Hades', confidence: 1 }, details: null }
        return status
      },
      'GET ./api/apps': () => ({ apps: [{ name: 'Other' }, saved] }),
      'POST ./api/apps': { status: true },
    })
    const w = track(mountNova(AppEditor, { props: { open: true, app: GAME, index: 1, platform: 'linux', libraryApi: true } }))
    await nextTick()
    buttonByText('Metadata').click()
    await flushPromises()
    expect(document.getElementById('nv-app-editor').style.display).toBe('none')
    buttonByText('Change match').click()
    await flushPromises()
    buttonByText('Use').click()
    await flushPromises()
    expect(document.body.textContent).toContain('IGDB · 7')
    document.getElementById('nv-app-editor').dispatchEvent(new Event('submit', { cancelable: true }))
    await flushPromises()
    const body = JSON.parse(calls(fetch, 'POST ./api/apps')[0][1].body)
    expect(body['nova-igdb-id']).toBe(7)
    expect(w.vm.isDirty).toBe(false)
  })
})

describe('MatchResults', () => {
  it('lists every candidate with year, edition, source and type, ten at a time', async () => {
    const candidates = Array.from({ length: 12 }, (_, i) => ({
      key: `steam-${i}`, source: 'steam', id: i + 1, appid: i + 1, name: `Game ${i}`, year: '2015', edition: i === 0 ? 'Legacy' : '',
      type: i === 1 ? 'dlc' : 'game', unlisted: i === 0, poster: i === 0 ? 'c1abcdef' : null, confidence: 0.9,
    }))
    const w = track(mountNova(MatchResults, { props: { candidates } }))
    expect(w.findAll('.nv-matches__item')).toHaveLength(10)
    const first = w.find('[data-key="steam-0"]').text()
    expect(first).toContain('2015 · Legacy · Steam 1')
    expect(first).toContain('Not sold any more')
    expect(w.find('[data-key="steam-1"]').text()).toContain('DLC')
    expect(w.find('[data-key="steam-0"] img').attributes('src')).toBe('./api/library/candidates/c1abcdef')
    await w.findAll('button').find((b) => b.text() === 'Show more').trigger('click')
    expect(w.findAll('.nv-matches__item')).toHaveLength(12)
    await w.findAll('button').find((b) => b.text() === 'Use').trigger('click')
    expect(w.emitted('choose')[0][0].appid).toBe(1)
  })
})

describe('AppArtworkPanel', () => {
  const APP = { name: 'Hades', 'nova-steam-appid': 1145360, 'image-path': '/c/p.png', 'nova-hero': '/c/h.jpg' }
  const SEARCH = {
    matches: [],
    artwork: {
      poster: [{ id: 'c1aaaaaa', label: 'Steam' }], hero: [{ id: 'c2aaaaaa', label: 'SteamGridDB' }], logo: [], icon: [],
      background: [{ id: 'c3aaaaaa', label: 'Steam' }],
    },
  }

  it('sets a kind from a pasted URL through the custom artwork job', async () => {
    const fetch = routeFetch({
      'GET ./api/library/artwork/search?appid=1145360': SEARCH,
      'POST ./api/library/artwork/custom': { job_id: 'u1' },
      'GET ./api/library/jobs/u1': { state: 'done', result: { applied: true, cleared: false } },
    })
    const w = track(mountNova(AppArtworkPanel, { props: { index: 2, app: APP, name: 'Hades' } }))
    await flushPromises()
    await w.findAll('button').find((b) => b.text() === 'Banner').trigger('click')
    await w.find('input[type="url"]').setValue('https://example.com/hero.jpg')
    await w.find('form').trigger('submit')
    await flushPromises()
    expect(JSON.parse(calls(fetch, 'POST ./api/library/artwork/custom')[0][1].body))
      .toEqual({ app_index: 2, kind: 'hero', url: 'https://example.com/hero.jpg' })
    expect(w.emitted('artwork-applied')).toHaveLength(1)
    expect(w.text()).toContain('Banner updated.')
  })

  it('offers found artwork per kind, and a background falls back to the banner preview', async () => {
    const fetch = routeFetch({
      'GET ./api/library/artwork/search?appid=1145360': SEARCH,
      'POST ./api/library/artwork/apply': { job_id: 'a2' },
      'GET ./api/library/jobs/a2': { state: 'done', result: {} },
    })
    const w = track(mountNova(AppArtworkPanel, { props: { index: 2, app: APP, name: 'Hades' } }))
    await flushPromises()
    await w.findAll('button').find((b) => b.text() === 'Background').trigger('click')
    expect(w.find('.nv-artpanel__preview img').attributes('src')).toContain('./api/covers/2/hero')
    await w.find('.nv-artpanel__option').trigger('click')
    await flushPromises()
    expect(JSON.parse(calls(fetch, 'POST ./api/library/artwork/apply')[0][1].body)).toEqual({ app_index: 2, background: 'c3aaaaaa' })
  })

  it('resets one kind to automatic and says when nothing automatic was found', async () => {
    const fetch = routeFetch({
      'GET ./api/library/artwork/search?appid=1145360': SEARCH,
      'POST ./api/library/artwork/custom': { job_id: 'r2' },
      'GET ./api/library/jobs/r2': { state: 'done', result: { applied: false, cleared: true } },
    })
    const w = track(mountNova(AppArtworkPanel, { props: { index: 2, app: APP, name: 'Hades' } }))
    await flushPromises()
    await w.findAll('button').find((b) => b.text() === 'Logo').trigger('click')
    await w.findAll('button').find((b) => b.text() === 'Reset to automatic').trigger('click')
    await flushPromises()
    expect(JSON.parse(calls(fetch, 'POST ./api/library/artwork/custom')[0][1].body)).toEqual({ app_index: 2, kind: 'logo', reset: true })
    expect(w.text()).toContain('No automatic Logo found')
  })

  it('shows the error the host gives for a refused URL', async () => {
    routeFetch({
      'GET ./api/library/artwork/search?appid=1145360': SEARCH,
      'POST ./api/library/artwork/custom': { job_id: 'e1' },
      'GET ./api/library/jobs/e1': { state: 'failed', error: 'Links to local or private network addresses aren’t allowed.' },
    })
    const w = track(mountNova(AppArtworkPanel, { props: { index: 2, app: APP, name: 'Hades' } }))
    await flushPromises()
    await w.find('input[type="url"]').setValue('https://192.168.1.1/x.png')
    await w.find('form').trigger('submit')
    await flushPromises()
    expect(w.text()).toContain('private network addresses')
  })

  it('links artwork sites with the title searched', () => {
    const sites = artworkSites('Grand Theft Auto V')
    expect(sites.map((s) => s.url)).toEqual([
      'https://www.steamgriddb.com/search/grids?term=Grand%20Theft%20Auto%20V',
      'https://www.igdb.com/search?q=Grand%20Theft%20Auto%20V',
      'https://thegamesdb.net/search.php?name=Grand%20Theft%20Auto%20V',
      'https://www.mobygames.com/search/?q=Grand%20Theft%20Auto%20V',
    ])
  })
})

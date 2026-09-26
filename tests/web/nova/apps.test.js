import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { nextTick } from 'vue'
import { flushPromises } from '@vue/test-utils'

import { mountNova } from './helpers.js'
import {
  appFlags, buildPayload, formFromApp, formsDiffer, newAppForm, newPrepCmd, validateForm, visibleApps,
} from '../../../src_assets/common/assets/web/nova/pages/apps/appForm.js'
import { coverFromGame, searchBucket, searchCovers } from '../../../src_assets/common/assets/web/nova/pages/apps/covers.js'
import AppEditorDialog from '../../../src_assets/common/assets/web/nova/pages/apps/AppEditorDialog.vue'
import AppItem from '../../../src_assets/common/assets/web/nova/pages/apps/AppItem.vue'
import PrepCommandList from '../../../src_assets/common/assets/web/nova/pages/apps/PrepCommandList.vue'
import DetachedCommandList from '../../../src_assets/common/assets/web/nova/pages/apps/DetachedCommandList.vue'

const wrappers = []
function track(w) {
  wrappers.push(w)
  return w
}
afterEach(() => {
  wrappers.splice(0).forEach((w) => w.unmount())
  document.body.innerHTML = ''
  vi.restoreAllMocks()
})

// What the previous Bootstrap editor sent for a new app (Apps.vue newApp()).
const OLD_NEW_APP = {
  name: '', output: '', cmd: '', index: -1, 'exclude-global-prep-cmd': false, elevated: false,
  'auto-detach': true, 'wait-all': true, 'exit-timeout': 5, 'prep-cmd': [], detached: [], 'image-path': '',
}

const STORED = {
  name: 'Steam Big Picture',
  cmd: 'setsid steam steam://open/bigpicture',
  'image-path': 'steam.png',
  'prep-cmd': [{ do: '', undo: 'setsid steam steam://close/bigpicture' }],
  'auto-detach': true,
  'wait-all': true,
  'exit-timeout': 5,
  'custom-key-from-another-fork': { keep: true },
}

describe('appForm', () => {
  it('new form carries every field the previous editor sent, plus working-dir', () => {
    const form = newAppForm()
    expect(Object.keys(form).sort()).toEqual([...Object.keys(OLD_NEW_APP), 'working-dir'].sort())
    for (const [k, v] of Object.entries(OLD_NEW_APP)) expect(form[k]).toEqual(v)
  })

  it('loading an app applies the previous defaults and keeps unknown keys', () => {
    const form = formFromApp(STORED, 3, 'linux')
    expect(form.index).toBe(3)
    expect(form['exclude-global-prep-cmd']).toBe(false)
    expect(form.detached).toEqual([])
    expect(form['custom-key-from-another-fork']).toEqual({ keep: true })
    expect('elevated' in form).toBe(false)
    expect(formFromApp(STORED, 3, 'windows').elevated).toBe(false)
    // editing the form never touches the stored app
    form['prep-cmd'][0].do = 'x'
    expect(STORED['prep-cmd'][0].do).toBe('')
  })

  it('payload has the same shape as before: the app plus index', () => {
    const payload = buildPayload(formFromApp(STORED, 3, 'linux'))
    expect(payload.index).toBe(3)
    expect(payload.name).toBe('Steam Big Picture')
    expect(payload['custom-key-from-another-fork']).toEqual({ keep: true })
    expect(payload['prep-cmd']).toEqual(STORED['prep-cmd'])
    expect(payload['exit-timeout']).toBe(5)
  })

  it('payload drops blank rows, strips quotes from the image path and omits an empty timeout', () => {
    const form = { ...newAppForm(), name: '  Game  ', 'image-path': '"/covers/a.png"', detached: ['', 'app --bg', '  '],
      'prep-cmd': [{ do: '', undo: '' }, { do: 'a', undo: '' }], 'exit-timeout': null }
    const payload = buildPayload(form)
    expect(payload.name).toBe('Game')
    expect(payload['image-path']).toBe('/covers/a.png')
    expect(payload.detached).toEqual(['app --bg'])
    expect(payload['prep-cmd']).toEqual([{ do: 'a', undo: '' }])
    expect('exit-timeout' in payload).toBe(false)
  })

  it('validates name and exit timeout', () => {
    expect(validateForm({ ...newAppForm(), name: '  ' }).name).toBe('nova.apps.error_name_required')
    expect(validateForm({ ...newAppForm(), name: 'A', 'exit-timeout': -1 }).exitTimeout).toBeTruthy()
    expect(validateForm({ ...newAppForm(), name: 'A', 'exit-timeout': 2.5 }).exitTimeout).toBeTruthy()
    expect(validateForm({ ...newAppForm(), name: 'A', 'exit-timeout': null })).toEqual({})
    expect(validateForm({ ...newAppForm(), name: 'A', 'exit-timeout': 0 })).toEqual({})
  })

  it('prep rows get an elevated flag only on Windows', () => {
    expect(newPrepCmd('windows')).toEqual({ do: '', undo: '', elevated: false })
    expect(newPrepCmd('linux')).toEqual({ do: '', undo: '' })
  })

  it('detects changes', () => {
    const a = newAppForm()
    expect(formsDiffer(a, newAppForm())).toBe(false)
    expect(formsDiffer(a, { ...newAppForm(), name: 'x' })).toBe(true)
  })

  it('flags describe non-default behaviour only', () => {
    expect(appFlags({ 'auto-detach': true })).toEqual([])
    expect(appFlags({ elevated: true, detached: ['a'], 'prep-cmd': [{}], 'exclude-global-prep-cmd': true })).toHaveLength(4)
  })

  it('filters by name or command and sorts while keeping host indexes', () => {
    const apps = [{ name: 'beta', cmd: 'b' }, { name: 'Alpha', cmd: 'steam' }, { name: 'gamma', cmd: 'c' }]
    expect(visibleApps(apps, '', 'default').map((x) => x.index)).toEqual([0, 1, 2])
    expect(visibleApps(apps, '', 'asc').map((x) => x.index)).toEqual([1, 0, 2])
    expect(visibleApps(apps, '', 'desc').map((x) => x.index)).toEqual([2, 0, 1])
    expect(visibleApps(apps, 'STEAM', 'default').map((x) => x.index)).toEqual([1])
    expect(visibleApps(null, '', 'default')).toEqual([])
  })
})

describe('covers', () => {
  it('buckets names like GameDB', () => {
    expect(searchBucket('Grand Theft Auto V')).toBe('gr')
    expect(searchBucket('!!')).toBe('@')
  })

  it('builds IGDB urls from a game record', () => {
    const c = coverFromGame({ id: 7, name: 'GTA V', cover: { url: '//images.igdb.com/igdb/image/upload/t_thumb/co2lbd.jpg' } })
    expect(c).toEqual({ name: 'GTA V', key: 'igdb_7', url: 'https://images.igdb.com/igdb/image/upload/t_cover_big/co2lbd.jpg',
      saveUrl: 'https://images.igdb.com/igdb/image/upload/t_cover_big_2x/co2lbd.png' })
    expect(coverFromGame({ name: 'x' })).toBeNull()
  })

  it('searches the bucket and keeps prefix matches', async () => {
    const fetcher = vi.fn(async (url) => ({
      ok: true,
      json: async () => (url.includes('/buckets/')
        ? { 1: { name: 'Portal 2' }, 2: { name: 'Portal' }, 3: { name: 'Postal' } }
        : { id: url.match(/games\/(\d+)/)[1], name: 'P', cover: { url: '//x/t_thumb/abc.jpg' } }),
    }))
    const found = await searchCovers('Portal', fetcher)
    expect(found.map((c) => c.key).sort()).toEqual(['igdb_1', 'igdb_2'])
    expect(await searchCovers('', fetcher)).toEqual([])
  })
})

describe('command lists', () => {
  it('prep list emits new arrays and never mutates the old one', async () => {
    const rows = [{ do: 'a', undo: 'b' }]
    const w = track(mountNova(PrepCommandList, { props: { modelValue: rows, platform: 'linux' } }))
    const input = w.find('input')
    await input.setValue('changed')
    const emitted = w.emitted('update:modelValue')
    expect(emitted.at(-1)[0]).toEqual([{ do: 'changed', undo: 'b' }])
    expect(rows[0].do).toBe('a')
    expect(w.findAll('[role="switch"]')).toHaveLength(0)
  })

  it('prep list shows the admin switch on Windows', () => {
    const w = track(mountNova(PrepCommandList, { props: { modelValue: [{ do: '', undo: '', elevated: false }], platform: 'windows' } }))
    expect(w.find('[role="switch"]').exists()).toBe(true)
  })

  it('detached list adds and removes rows', async () => {
    const w = track(mountNova(DetachedCommandList, { props: { modelValue: ['one'] } }))
    const remove = w.findAll('button').find((b) => b.attributes('aria-label')?.startsWith('Remove background'))
    await remove.trigger('click')
    expect(w.emitted('update:modelValue').at(-1)[0]).toEqual([])
  })
})

describe('AppItem', () => {
  it('names the delete button after the app and falls back to an initial', () => {
    const w = track(mountNova(AppItem, { props: { app: { name: 'Desktop' }, coverUrl: '' } }))
    expect(w.find('[aria-label="Delete Desktop"]').exists()).toBe(true)
    expect(w.find('.nv-app__initial').text()).toBe('D')
    expect(w.find('.nv-app__open').attributes('aria-label')).toBe('Edit Desktop')
  })
})

describe('AppEditorDialog', () => {
  let fetchMock
  beforeEach(() => {
    fetchMock = vi.fn(async () => ({ ok: true, status: 200, statusText: 'OK', json: async () => ({ status: true }), clone() { return this } }))
    vi.stubGlobal('fetch', fetchMock)
  })
  afterEach(() => vi.unstubAllGlobals())

  function dialog() {
    return document.body.querySelector('[role="dialog"]')
  }

  it('shows an error instead of saving without a name', async () => {
    track(mountNova(AppEditorDialog, { props: { open: true, app: null, index: -1, platform: 'linux' } }))
    await nextTick()
    document.body.querySelector('button[form="nv-app-editor"]').click()
    await flushPromises()
    expect(dialog().textContent).toContain('Enter a name')
    expect(fetchMock).not.toHaveBeenCalled()
  })

  it('posts the payload to /api/apps and emits saved', async () => {
    const w = track(mountNova(AppEditorDialog, { props: { open: true, app: STORED, index: 2, platform: 'linux' } }))
    await nextTick()
    document.getElementById('nv-app-editor').dispatchEvent(new Event('submit', { cancelable: true }))
    await flushPromises()
    expect(fetchMock).toHaveBeenCalledTimes(1)
    const [url, options] = fetchMock.mock.calls[0]
    expect(url).toBe('./api/apps')
    expect(options.method).toBe('POST')
    const body = JSON.parse(options.body)
    expect(body.index).toBe(2)
    expect(body.name).toBe('Steam Big Picture')
    expect(w.emitted('saved')).toHaveLength(1)
  })

  it('asks before discarding edits on Escape', async () => {
    const w = track(mountNova(AppEditorDialog, { props: { open: true, app: null, index: -1, platform: 'linux' } }))
    await nextTick()
    const name = document.body.querySelector('[role="dialog"] input')
    name.value = 'New game'
    name.dispatchEvent(new Event('input'))
    await nextTick()
    document.body.querySelector('.nv-dialog').dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))
    await nextTick()
    const titles = [...document.body.querySelectorAll('[role="dialog"] h2')].map((h) => h.textContent)
    expect(titles).toContain('Discard your changes?')
    expect(w.emitted('update:open')).toBeUndefined()
  })

  it('closes straight away on Escape when nothing changed', async () => {
    const w = track(mountNova(AppEditorDialog, { props: { open: true, app: STORED, index: 0, platform: 'linux' } }))
    await nextTick()
    document.body.querySelector('.nv-dialog').dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))
    await nextTick()
    expect(w.emitted('update:open').at(-1)).toEqual([false])
  })
})

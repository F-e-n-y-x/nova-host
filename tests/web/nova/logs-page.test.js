import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { flushPromises } from '@vue/test-utils'
import { nextTick, ref } from 'vue'

import { mountNova } from './helpers.js'
import {
  LEVEL_GROUPS, PAGE_SIZE, annotate, countByGroup, filterEntries, highlightParts, levelGroup, logFileName,
  problemIndexes, stepProblem, toText, windowEntries,
} from '../../../src_assets/common/assets/web/nova/pages/logs/logView.js'
import { compareDriverVersions, parseDriverVersion } from '../../../src_assets/common/assets/web/nova/pages/logs/driverVersion.js'
import { releaseState } from '../../../src_assets/common/assets/web/nova/pages/logs/useVirtualInput.js'
import { useLogView } from '../../../src_assets/common/assets/web/nova/pages/logs/useLogView.js'
import LogViewer from '../../../src_assets/common/assets/web/nova/pages/logs/LogViewer.vue'
import LogToolbar from '../../../src_assets/common/assets/web/nova/pages/logs/LogToolbar.vue'
import DiagnosticsPanel from '../../../src_assets/common/assets/web/nova/pages/logs/DiagnosticsPanel.vue'
import { toasts } from '../../../src_assets/common/assets/web/nova/toast.js'

const LOG = [
  '[2026-09-27 03:09:01.560]: Info: Zenith version: 0.1.0',
  '[2026-09-27 03:09:02.000]: Warning: [h264_nvenc @ 0x1] Multiple reference frames are not supported',
  '[2026-09-27 03:09:02.100]: Error: Could not open codec [h264_nvenc]: Function not implemented',
  '[2026-09-27 03:09:03.000]: Debug: probing vaapi',
  '[2026-09-27 03:09:04.786]: Info: Found H.264 encoder: h264_nvenc [nvenc]',
].join('\n')

const entriesOf = (text) => annotate(text.split('\n').map((line) => {
  const m = /^\[([^\]]+)]: (\w+): (.*)$/.exec(line)
  return { timestamp: m[1], level: m[2], message: m[3] }
}))

const wrappers = []
const track = (w) => { wrappers.push(w); return w }

afterEach(() => {
  while (wrappers.length) wrappers.pop().unmount()
  document.body.innerHTML = ''
  toasts.splice(0)
  vi.restoreAllMocks()
  vi.unstubAllGlobals()
})

describe('logView helpers', () => {
  it('maps levels to chip groups', () => {
    expect(levelGroup('Fatal')).toBe('error')
    expect(levelGroup('error')).toBe('error')
    expect(levelGroup('Warning')).toBe('warning')
    expect(levelGroup('Verbose')).toBe('debug')
    expect(levelGroup('Whatever')).toBe('info')
    expect(LEVEL_GROUPS.map((g) => g.id)).toEqual(['error', 'warning', 'info', 'debug'])
  })

  it('counts, filters by group and text, and windows the newest entries', () => {
    const entries = entriesOf(LOG)
    expect(countByGroup(entries)).toEqual({ error: 1, warning: 1, info: 2, debug: 1 })
    const groups = new Set(['error', 'warning'])
    expect(filterEntries(entries, { groups }).map((e) => e.level)).toEqual(['Warning', 'Error'])
    expect(filterEntries(entries, { groups: new Set(['info']), query: 'FOUND' })).toHaveLength(1)
    expect(filterEntries(entries, { groups: new Set(['warning']), query: 'warning' })).toHaveLength(1)
    expect(windowEntries(entries, 2)).toEqual({ visible: entries.slice(3), hidden: 3 })
    expect(windowEntries(entries, 10).hidden).toBe(0)
  })

  it('steps through warnings and errors', () => {
    const problems = problemIndexes(entriesOf(LOG))
    expect(problems).toEqual([1, 2])
    expect(stepProblem(problems, -1, 'next')).toBe(1)
    expect(stepProblem(problems, 1, 'next')).toBe(2)
    expect(stepProblem(problems, 2, 'next')).toBe(-1)
    expect(stepProblem(problems, -1, 'prev')).toBe(2)
    expect(stepProblem(problems, 2, 'prev')).toBe(1)
    expect(stepProblem(problems, 1, 'prev')).toBe(-1)
    expect(stepProblem([], -1, 'next')).toBe(-1)
  })

  it('splits text around case-insensitive matches without HTML', () => {
    expect(highlightParts('Found <b>NVENC</b> nvenc', 'nvenc')).toEqual([
      { text: 'Found <b>', match: false },
      { text: 'NVENC', match: true },
      { text: '</b> ', match: false },
      { text: 'nvenc', match: true },
    ])
    expect(highlightParts('abc', '')).toEqual([{ text: 'abc', match: false }])
  })

  it('round-trips entries to log text and names downloads', () => {
    expect(toText(entriesOf(LOG).slice(0, 1))).toBe('[2026-09-27 03:09:01.560]: Info: Zenith version: 0.1.0')
    expect(logFileName(new Date(2026, 8, 27, 3, 15, 1))).toBe('nova-host-log-20260927-031501.log')
  })
})

describe('useLogView', () => {
  it('derives rows, resets selection on filter change, and widens the window to reach old problems', async () => {
    const lines = [LOG.split('\n')[2]]
    for (let i = 0; i < PAGE_SIZE + 5; i++) lines.push(`[2026-09-27 03:10:00.000]: Info: line ${i}`)
    const text = ref(lines.join('\n'))
    const view = useLogView(text)
    expect(view.windowed.value.hidden).toBe(6)
    expect(view.step('next')).toBe(0)
    expect(view.windowed.value.hidden).toBe(0)
    expect(view.rows.value[0]).toMatchObject({ selected: true, group: 'error', time: '03:09:02.100', iso: '2026-09-27T03:09:02.100' })
    view.setQuery('line 3')
    await nextTick()
    expect(view.selected.value).toBe(-1)
    expect(view.filtersActive.value).toBe(true)
    view.clearFilters()
    await nextTick()
    expect(view.filtersActive.value).toBe(false)
  })
})

describe('LogToolbar', () => {
  it('exposes pressed state on level chips and emits toggles', async () => {
    const w = track(mountNova(LogToolbar, { props: { chips: [{ id: 'error', count: 2, on: true }, { id: 'debug', count: 0, on: false }] } }))
    const chips = w.findAll('.nv-chip')
    expect(chips[0].attributes('aria-pressed')).toBe('true')
    expect(chips[1].attributes('aria-pressed')).toBe('false')
    expect(chips[0].text()).toContain('Errors')
    await chips[1].trigger('click')
    expect(w.emitted('toggle-group')[0]).toEqual(['debug'])
    expect(w.text()).toContain('No warnings or errors')
  })
})

describe('LogViewer', () => {
  it('renders entries as text, filters by level and query, and highlights matches', async () => {
    const w = track(mountNova(LogViewer, { props: { load: async () => LOG } }))
    await flushPromises()
    const rows = () => w.findAll('.nv-log__row')
    expect(rows()).toHaveLength(5)
    expect(w.get('.nv-log').attributes('role')).toBe('log')
    await w.findAll('.nv-chip').find((c) => c.text().startsWith('Info')).trigger('click')
    expect(rows()).toHaveLength(3)
    await w.get('input[type="search"]').setValue('codec')
    expect(rows()).toHaveLength(1)
    expect(w.get('mark').text()).toBe('codec')
    expect(w.text()).toContain('1 entries')
  })

  it('shows a retry alert when the log cannot load', async () => {
    const load = vi.fn().mockRejectedValueOnce(new Error('503 Service Unavailable')).mockResolvedValue(LOG)
    const w = track(mountNova(LogViewer, { props: { load } }))
    await flushPromises()
    expect(w.text()).toContain("Couldn't load the log.")
    await w.findAll('button').find((b) => b.text() === 'Try again').trigger('click')
    await flushPromises()
    expect(w.findAll('.nv-log__row')).toHaveLength(5)
  })

  it('copies the entries that are shown', async () => {
    const writeText = vi.fn().mockResolvedValue()
    vi.stubGlobal('navigator', { clipboard: { writeText } })
    const w = track(mountNova(LogViewer, { props: { load: async () => LOG } }))
    await flushPromises()
    await w.get('input[type="search"]').setValue('nvenc')
    await w.findAll('button').find((b) => b.text() === 'Copy').trigger('click')
    await flushPromises()
    expect(writeText.mock.calls[0][0].split('\n')).toHaveLength(3)
    expect(toasts[0].message).toBe('Copied 3 log entries.')
  })
})

describe('DiagnosticsPanel', () => {
  beforeEach(() => {
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: true, status: 200, json: async () => ({ status: true }) })))
  })

  it('shows platform actions only where they apply', () => {
    const linux = track(mountNova(DiagnosticsPanel, { props: { platform: 'linux' } }))
    const titles = linux.findAll('.nv-card__title').map((h) => h.text())
    expect(titles).toContain('Reset screen-sharing permission')
    expect(titles).not.toContain('Reset saved display settings')
    const windows = track(mountNova(DiagnosticsPanel, { props: { platform: 'windows' } }))
    expect(windows.findAll('.nv-card__title').map((h) => h.text())).toContain('Reset saved display settings')
  })

  it('asks before acting, then reports the result', async () => {
    const w = track(mountNova(DiagnosticsPanel, { props: { platform: 'linux' } }))
    await w.findAll('button').find((b) => b.text() === 'Force close').trigger('click')
    await flushPromises()
    const dialog = document.querySelector('[role="dialog"]')
    expect(dialog.textContent).toContain('Force close the running app')
    expect(fetch).not.toHaveBeenCalled()
    const confirm = [...dialog.querySelectorAll('button')].find((b) => b.textContent.trim() === 'Force close')
    confirm.click()
    await flushPromises()
    expect(fetch).toHaveBeenCalledWith('./api/apps/close', expect.objectContaining({ method: 'POST' }))
    expect(toasts[0].message).toBe('The running app was closed.')
    expect(document.querySelector('[role="dialog"]')).toBeNull()
  })
})

describe('driver versions', () => {
  it('compares dotted versions and release states', () => {
    expect(parseDriverVersion('v1.2.3')).toEqual([1, 2, 3])
    expect(parseDriverVersion('beta')).toBeNull()
    expect(compareDriverVersions('1.2', '1.2.0')).toBe(0)
    expect(compareDriverVersions('1.2', '1.3')).toBe(-1)
    expect(compareDriverVersions('x', '1')).toBeNull()
    const release = { loading: false, error: false, version: 'v2.0.0' }
    expect(releaseState({ installed: true, version: '2.0.0' }, release)).toBe('current')
    expect(releaseState({ installed: true, version: '1.9' }, release)).toBe('outdated')
    expect(releaseState({ installed: false }, release)).toBe('not_installed')
    expect(releaseState({ installed: true }, { ...release, loading: true })).toBe('loading')
    expect(releaseState({ installed: true }, { ...release, error: true })).toBe('unavailable')
  })
})

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { nextTick } from 'vue'
import { flushPromises } from '@vue/test-utils'

import { mountNova } from './helpers.js'
import {
  appFlags, buildPayload, formFromApp, perfForm, perfLauncher, perfPayload, perfSummary, validateForm,
} from '../../../src_assets/common/assets/web/nova/pages/apps/appForm.js'
import AppEditor from '../../../src_assets/common/assets/web/nova/pages/apps/AppEditor.vue'

const GAME = { name: 'Cyberpunk 2077', cmd: '"/usr/libexec/nova-host/nova-proton-run" "/g/Cyberpunk2077.exe"', 'nova-exe': '/g/Cyberpunk2077.exe' }

describe('performance profile form', () => {
  it('round-trips every setting and leaves defaults out', () => {
    const stored = { fps_cap: 60, fsr: 3, vkbasalt: true, vkbasalt_cas: 80, mangohud: true, bitrate_kbps: 40000, power: 'performance' }
    expect(perfPayload(perfForm({ 'nova-perf': stored }))).toEqual(stored)
    expect(perfPayload(perfForm({}))).toBeNull()
    expect(perfForm({}).power).toBe('default')
    expect(perfForm({ 'nova-perf': { bitrate_kbps: 25500 } }).bitrate_mbps).toBe(25.5)
  })

  it('clamps and drops values the host would reject', () => {
    expect(perfPayload({ ...perfForm(), fps_cap: 5000, bitrate_mbps: 0.1, power: 'turbo', fsr: '9' }))
      .toEqual({ fps_cap: 1000, bitrate_kbps: 500 })
    expect(perfPayload({ ...perfForm(), fps_cap: '', bitrate_mbps: '' })).toBeNull()
  })

  it('validates the frame rate and bitrate fields', () => {
    const form = formFromApp(GAME, 0, 'linux')
    expect(validateForm(form)).toEqual({})
    form['nova-perf'].fps_cap = 1001
    form['nova-perf'].bitrate_mbps = 900
    expect(validateForm(form)).toEqual({ perfFps: 'nova.apps.perf_error_fps', perfBitrate: 'nova.apps.perf_error_bitrate' })
  })

  it('names the launcher like the host does', () => {
    expect(perfLauncher(GAME)).toBe('proton')
    expect(perfLauncher({ cmd: 'steam steam://rungameid/1091500' })).toBe('steam')
    expect(perfLauncher({ cmd: 'env LUTRIS_SKIP_INIT=1 lutris lutris:rungameid/3' })).toBe('lutris')
    expect(perfLauncher({ cmd: '/usr/games/supertuxkart' })).toBe('command')
    expect(perfLauncher({ cmd: ' ' })).toBe('none')
  })

  it('flags and summarises apps with a profile', () => {
    const app = { ...GAME, 'nova-perf': { fps_cap: 60, fsr: 2, bitrate_kbps: 40000 } }
    expect(appFlags(app)).toContain('nova.apps.flag_perf')
    expect(appFlags(GAME)).not.toContain('nova.apps.flag_perf')
    expect(perfSummary(app)).toEqual(['60 fps', 'FSR 2', '40 Mbps'])
  })

  it('saves the profile as nova-perf', () => {
    const form = formFromApp(GAME, 0, 'linux')
    form['nova-perf'].bitrate_mbps = 30
    form['nova-perf'].power = 'balanced'
    expect(buildPayload(form)['nova-perf']).toEqual({ bitrate_kbps: 30000, power: 'balanced' })
  })
})

describe('AppEditor Performance section', () => {
  let fetchMock
  beforeEach(() => {
    fetchMock = vi.fn(async () => ({ ok: true, status: 200, statusText: 'OK', json: async () => ({ status: true }), clone() { return this } }))
    vi.stubGlobal('fetch', fetchMock)
  })
  afterEach(() => {
    vi.unstubAllGlobals()
    document.body.innerHTML = ''
  })

  it('shows the section on Linux and posts the profile', async () => {
    const w = mountNova(AppEditor, { props: { open: true, app: GAME, index: 0, platform: 'linux' } })
    await nextTick()
    const section = document.body.querySelector('[aria-labelledby="nv-editor-perf"]')
    expect(section).not.toBeNull()
    expect(section.textContent).toContain('Stream bitrate limit')
    expect(section.textContent).toContain('Power mode while streaming')
    const fps = document.getElementById('nv-app-perf-fps')
    fps.value = '72'
    fps.dispatchEvent(new Event('input'))
    await nextTick()
    document.getElementById('nv-app-editor').dispatchEvent(new Event('submit', { cancelable: true }))
    await flushPromises()
    const body = JSON.parse(fetchMock.mock.calls[0][1].body)
    expect(body['nova-perf']).toEqual({ fps_cap: 72 })
    w.unmount()
  })

  it('explains that launch settings do not reach Steam games', async () => {
    const w = mountNova(AppEditor, { props: { open: true, app: { name: 'Portal 2', cmd: 'steam steam://rungameid/620' }, index: 0, platform: 'linux' } })
    await nextTick()
    const section = document.body.querySelector('[aria-labelledby="nv-editor-perf"]')
    expect(section.textContent).toContain("Steam starts this game")
    expect(document.getElementById('nv-app-perf-fps').disabled).toBe(true)
    expect(document.getElementById('nv-app-perf-bitrate').disabled).toBe(false)
    w.unmount()
  })
})

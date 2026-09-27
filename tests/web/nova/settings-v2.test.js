import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushPromises } from '@vue/test-utils'
import { mountNova } from './helpers.js'
import HealthPanel from '../../../src_assets/common/assets/web/nova/pages/logs/HealthPanel.vue'
import SettingControl from '../../../src_assets/common/assets/web/configs/components/SettingControl.vue'
import {
  encoderSummary, hostEncoders, pickerChoices, resetHostProbes, useHostProbes,
} from '../../../src_assets/common/assets/web/configs/hostProbes.js'
import { isModified, SECRET_MASK } from '../../../src_assets/common/assets/web/configs/settings_model.js'

const t = (key, params = {}) => `${key}${Object.keys(params).length ? JSON.stringify(params) : ''}`

function mockFetch(routes) {
  vi.stubGlobal('fetch', vi.fn(async (url) => {
    const body = routes[String(url)]
    if (body === undefined) return { ok: false, status: 404, json: async () => ({}) }
    return { ok: true, status: 200, json: async () => body }
  }))
}

afterEach(() => {
  vi.unstubAllGlobals()
  resetHostProbes()
  document.body.innerHTML = ''
})

describe('host probes', () => {
  it('turns displays and sinks into choices and keeps an unknown current value', async () => {
    mockFetch({
      './api/displays': { displays: [
        { name: 'HDMI-0', connected: true, primary: true, width: 1920, height: 1080, refresh_hz: 60 },
        { name: 'DP-0', connected: false },
      ] },
      './api/audio/sinks': { sinks: [{ name: 'sink-sunshine-stereo', description: 'Nova stereo', virtual: true }] },
    })
    await useHostProbes().ready
    const displays = pickerChoices('displays', 'DP-9', t)
    expect(displays.map((c) => c.value)).toEqual(['', 'HDMI-0', 'DP-9'])
    expect(displays[1].label).toContain('1920×1080')
    const sinks = pickerChoices('sinks', '', t)
    expect(sinks[1].label).toContain('nova.settings.picker_virtual')
  })

  it('returns null when the host has no such API, so the page keeps the text field', async () => {
    mockFetch({})
    await useHostProbes().ready
    expect(pickerChoices('displays', '', t)).toBeNull()
    expect(hostEncoders()).toBeNull()
    expect(encoderSummary(t)).toBe('')
  })

  it('reports the detected encoder from host info', async () => {
    mockFetch({ './api/host/info': { gpu: [{ name: 'NVIDIA GeForce GTX 1080 Ti' }], encoders: { active: 'nvenc', codecs: ['H.264', 'HEVC'] } } })
    await useHostProbes().ready
    expect(hostEncoders()).toEqual(['nvenc'])
    expect(encoderSummary(t)).toContain('"gpu":"GeForce GTX 1080 Ti"')
  })
})

describe('SettingControl', () => {
  it('shows a display picker when the host lists displays, a text field otherwise', async () => {
    mockFetch({ './api/displays': { displays: [{ name: 'HDMI-0', connected: true }] } })
    const w = mountNova(SettingControl, { props: { optionKey: 'output_name', label: 'Display', modelValue: '' } })
    expect(w.find('input').exists()).toBe(true)
    await flushPromises()
    expect(w.find('select').exists()).toBe(true)
    expect(w.findAll('option').map((o) => o.attributes('value'))).toContain('HDMI-0')
  })

  it('masks a stored secret and says a key is saved', () => {
    mockFetch({})
    const w = mountNova(SettingControl, { props: { optionKey: 'steamgriddb_api_key', label: 'Key', modelValue: SECRET_MASK } })
    expect(w.find('input').attributes('type')).toBe('password')
    expect(w.text()).toContain('A key is saved')
  })
})

describe('secret options', () => {
  it('count a stored key as changed from the empty default', () => {
    expect(isModified('steamgriddb_api_key', SECRET_MASK)).toBe(true)
    expect(isModified('steamgriddb_api_key', '')).toBe(false)
  })
})

describe('HealthPanel', () => {
  it('lists problems first with a copyable fix and a link to the setting', async () => {
    const load = async () => ({ checks: [
      { id: 'encoder', status: 'ok', title: 'GPU encoding', detail: 'NVENC.', fix: null },
      { id: 'clipboard', status: 'warn', title: 'Clipboard', detail: 'xclip missing.', fix: { kind: 'command', value: 'sudo apt install xclip' } },
      { id: 'web_ui_wan', status: 'warn', title: 'Web UI', detail: 'Exposed.', fix: { kind: 'setting', value: 'origin_web_ui_allowed' } },
    ] })
    const w = mountNova(HealthPanel, { props: { load } })
    await flushPromises()
    const titles = w.findAll('.nv-checks__title').map((n) => n.text())
    expect(titles[titles.length - 1]).toBe('GPU encoding')
    expect(w.find('.nv-checks__code').text()).toBe('sudo apt install xclip')
    expect(w.find('a.nv-checks__link').attributes('href')).toContain('/settings#origin_web_ui_allowed')
    expect(w.text()).toContain('2 need attention')
  })

  it('hides itself when the host has no health API', async () => {
    const w = mountNova(HealthPanel, { props: { load: async () => null } })
    await flushPromises()
    expect(w.find('.nv-checks').exists()).toBe(false)
  })
})

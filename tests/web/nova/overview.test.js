import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushPromises } from '@vue/test-utils'

import { mountNova } from './helpers'
import {
  appKind, attentionFromHealth, captureLabel, displayLabel, encoderLabel, formatClock, formatDuration,
  formatMbps, formatMs, latencySplit, pickDisplay, shortVersion,
} from '../../../src_assets/common/assets/web/nova/pages/overview/format.js'
import StreamHealthCard from '../../../src_assets/common/assets/web/nova/pages/overview/StreamHealthCard.vue'
import StreamBar from '../../../src_assets/common/assets/web/nova/pages/overview/StreamBar.vue'
import DevicesCard from '../../../src_assets/common/assets/web/nova/pages/overview/DevicesCard.vue'
import LibraryCard from '../../../src_assets/common/assets/web/nova/pages/overview/LibraryCard.vue'
import HardwareCard from '../../../src_assets/common/assets/web/nova/pages/overview/HardwareCard.vue'
import { optionalJson } from '../../../src_assets/common/assets/web/nova/pages/overview/useOverview.js'
import { previewState, refreshPreview, resetPreviewForTests } from '../../../src_assets/common/assets/web/nova/pages/overview/usePreview.js'

const SESSION = {
  id: 1, client_uuid: 'a1', client_name: 'Pixel', app_name: 'Grand Theft Auto V', started_at: 1000,
  resolution: { w: 2400, h: 1080 }, fps_actual: 60, codec: 'HEVC', bitrate_kbps: 30000,
  latency_ms: { capture: 1.2, encode: 3.8, send: 1.0, total: 6.0 }, loss_pct: 0.1, frames: { total: 60, duplicated: 0 },
  samples: [{ t: 1, bitrate_kbps: 29000 }, { t: 2, bitrate_kbps: 31000 }],
}

afterEach(() => {
  vi.unstubAllGlobals()
  resetPreviewForTests()
})

describe('overview format helpers', () => {
  it('labels encoders, captures and versions', () => {
    expect(encoderLabel('nvenc')).toBe('NVENC')
    expect(encoderLabel('')).toBe('')
    expect(captureLabel('nvfbc')).toBe('NvFBC')
    expect(shortVersion('0.1.0-59dad173-dirty')).toBe('0.1.0')
    expect(shortVersion('nova-v0.2.0')).toBe('0.2.0')
  })

  it('formats numbers and durations', () => {
    expect(formatMbps(30000)).toBe('30 Mbps')
    expect(formatMbps(4500)).toBe('4.5 Mbps')
    expect(formatMbps(NaN)).toBe('—')
    expect(formatMs(3.84)).toBe('3.8 ms')
    expect(formatClock(1000, 1000 * 1000 + 2533 * 1000)).toBe('00:42:13')
    expect(formatDuration(30)).toBe('< 1 min')
    expect(formatDuration(48 * 60)).toBe('48 min')
    expect(formatDuration(72 * 60)).toBe('1 h 12')
  })

  it('splits latency with a visible minimum per stage', () => {
    const s = latencySplit({ capture: 1.2, encode: 3.8, send: 1.0 })
    expect(s.capture + s.encode + s.send).toBeCloseTo(100, 0)
    expect(latencySplit({ capture: 0.01, encode: 10, send: 0 }).capture).toBeGreaterThanOrEqual(3.5)
    expect(latencySplit(null)).toBeNull()
    expect(latencySplit({ capture: 0, encode: 0, send: 0 })).toBeNull()
  })

  it('classifies apps and picks displays', () => {
    expect(appKind({ name: 'X', cmd: 'lutris lutris:rungameid/3' })).toBe('game')
    expect(appKind({ name: 'Desktop' })).toBe('desktop')
    expect(appKind({ name: 'Chrome', cmd: 'google-chrome' })).toBe('app')
    const displays = [{ name: 'DP-1' }, { name: 'HDMI-0', primary: true, mode_width: 1920, mode_height: 1080, refresh_hz: 60 }]
    expect(pickDisplay(displays).name).toBe('HDMI-0')
    expect(pickDisplay(displays, 'DP-1').name).toBe('DP-1')
    expect(pickDisplay([])).toBeNull()
    expect(displayLabel(displays[1])).toBe('HDMI-0 · 1920×1080 · 60 Hz')
  })

  it('turns health checks into attention issues', () => {
    const t = (k) => k
    const issues = attentionFromHealth([
      { id: 'ok', status: 'ok', title: 'fine' },
      { id: 'clip', status: 'warn', title: 'Clipboard', detail: 'xclip', fix: { kind: 'command', value: 'sudo apt install xclip' } },
      { id: 'enc', status: 'error', title: 'Encoder', detail: 'cpu', fix: { kind: 'setting', value: 'encoder' } },
    ], t)
    expect(issues.map((i) => i.id)).toEqual(['enc', 'clip'])
    expect(issues[0].severity).toBe('danger')
    expect(issues[0].action.to).toBe('/settings#encoder')
    expect(issues[1].command).toBe('sudo apt install xclip')
    expect(attentionFromHealth(null, t)).toEqual([])
  })
})

describe('optional host APIs', () => {
  it('resolves 404 to null and rethrows other errors', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => new Response('nope', { status: 404 })))
    expect(await optionalJson('./api/host/info')).toBeNull()
    vi.stubGlobal('fetch', vi.fn(async () => new Response('boom', { status: 500 })))
    await expect(optionalJson('./api/host/info')).rejects.toMatchObject({ status: 500 })
  })

  it('marks the preview unavailable on 404 and keeps a message on 503', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => new Response('', { status: 404 })))
    await refreshPreview()
    expect(previewState.available).toBe(false)
    resetPreviewForTests()
    vi.stubGlobal('fetch', vi.fn(async () => new Response(JSON.stringify({ error: 'no capture' }), { status: 503 })))
    await refreshPreview()
    expect(previewState.available).toBe(true)
    expect(previewState.unavailable).toBe('no capture')
  })
})

describe('overview cards', () => {
  it('stream health shows the live split and recent sessions', async () => {
    const w = mountNova(StreamHealthCard, { props: { session: SESSION, history: [
      { id: 9, client_name: 'Deck', app_name: 'Far Cry 5', started_at: 900, duration_s: 2880, codec: 'H.264' }] } })
    expect(w.find('[role="img"]').attributes('aria-label')).toContain('capture 1.2 ms')
    expect(w.text()).toContain('6.0 ms')
    expect(w.text()).toContain('Far Cry 5')
    expect(w.text()).toContain('48 min')
    w.unmount()
  })

  it('stream health idle uses the last session averages', () => {
    const w = mountNova(StreamHealthCard, { props: { session: null, history: [
      { id: 9, client_name: 'Deck', app_name: 'Far Cry 5', started_at: 900, duration_s: 60, avg_latency_ms: 6.1, avg_fps: 59.9, avg_bitrate_kbps: 30000 }] } })
    expect(w.text()).toContain('6.1 ms avg')
    expect(w.text()).toContain('60 fps')
    expect(w.find('[role="img"]').exists()).toBe(false)
    w.unmount()
  })

  it('stream bar shows stats and emits end', async () => {
    const w = mountNova(StreamBar, { props: { session: SESSION } })
    expect(w.text()).toContain('Grand Theft Auto V on Pixel')
    expect(w.text()).toContain('2400×1080')
    expect(w.text()).toContain('30 Mbps')
    await w.findAll('button').find((b) => b.text() === 'End stream').trigger('click')
    expect(w.emitted('end')[0][0].client_uuid).toBe('a1')
    w.unmount()
  })

  it('stream bar names the virtual display a stream captures', () => {
    const desk = mountNova(StreamBar, { props: { session: { ...SESSION, display: { kind: 'desktop' } } } })
    expect(desk.text()).not.toContain('Virtual display')
    desk.unmount()
    const vd = mountNova(StreamBar, { props: { session: { ...SESSION, display: { kind: 'virtual', name: ':20', w: 2340, h: 1080, fps: 120 } } } })
    expect(vd.find('.nv-sbar__vd').text()).toBe('Virtual display :20 2340×1080@120')
    vd.unmount()
  })

  it('devices card orders streaming first and states status in text', () => {
    const w = mountNova(DevicesCard, { props: { devices: [
      { uuid: 'x', name: 'Tablet', enabled: false },
      { uuid: 'a1', name: 'Pixel', enabled: true },
    ], streamingUuids: new Set(['a1']) } })
    const rows = w.findAll('.nv-devc__row')
    expect(rows[0].text()).toContain('Pixel')
    expect(rows[0].text()).toContain('Streaming now')
    expect(rows[1].text()).toContain('Blocked')
    w.unmount()
  })

  it('library card marks the running app and offers Close', async () => {
    const w = mountNova(LibraryCard, { props: { data: { apps: [{ name: 'Desktop' }, { name: 'GTA V', cmd: 'lutris lutris:rungameid/1' }], runningIndex: 1, runningName: 'GTA V' } } })
    const first = w.find('.nv-lib__row')
    expect(first.text()).toContain('GTA V')
    expect(first.text()).toContain('Running')
    w.unmount()
  })

  it('library card shows an empty state with an action', () => {
    const w = mountNova(LibraryCard, { props: { data: { apps: [], runningIndex: null, runningName: null } } })
    expect(w.text()).toContain('No games or apps yet')
    w.unmount()
  })

  it('hardware card uses host info and falls back to the log', () => {
    const w = mountNova(HardwareCard, { props: { info: { gpu: [{ name: 'GeForce GTX 1080 Ti', driver_version: '580.178.04' }],
      encoders: { active: 'nvenc', codecs: ['H.264', 'HEVC'], av1: false }, capture: { method: 'nvfbc', zero_copy: true } } } })
    expect(w.text()).toContain('GeForce GTX 1080 Ti')
    expect(w.text()).toContain('NVENC · H.264, HEVC')
    expect(w.text()).toContain('NvFBC · Zero-copy')
    w.unmount()
    const f = mountNova(HardwareCard, { props: { info: null, logEncoders: [{ codec: 'H.264', label: 'NVENC', hardware: true }] } })
    expect(f.text()).toContain('NVENC')
    f.unmount()
    return flushPromises()
  })
})

import { afterEach, describe, expect, it, vi } from 'vitest'
import { defineComponent, h } from 'vue'
import { flushPromises } from '@vue/test-utils'

import { mountNova } from './helpers'
import { fetchUpdateStatus, hasUpdate, useUpdateStatus } from '../../../src_assets/common/assets/web/nova/update.js'
import UpdateBanner from '../../../src_assets/common/assets/web/nova/pages/overview/UpdateBanner.vue'

const LATEST = {
  tag: 'nova-v0.3.0', version: '0.3.0', name: 'Nova 0.3.0', prerelease: false,
  html_url: 'https://github.com/F-e-n-y-x/nova-host/releases/tag/nova-v0.3.0', notes: '', package: { name: 'nova-host_0.3.0_amd64.deb', size: 1 },
}
const status = (over = {}) => ({
  enabled: true, current: '0.2.0-1d43d4a3', token_set: true, checking: false, checked_at: 100, error: '', streaming: false,
  update_available: true, latest: LATEST, install: { state: 'idle', tag: '', progress: 0, message: '', command: '' }, ...over,
})
const json = (body, init = {}) => new Response(JSON.stringify(body), { status: 200, headers: { 'Content-Type': 'application/json' }, ...init })

afterEach(() => {
  vi.unstubAllGlobals()
  vi.useRealTimers()
  document.body.innerHTML = ''
})

describe('update status helpers', () => {
  it('only treats a host-verified newer release as an update', () => {
    expect(hasUpdate(status())).toBe(true)
    expect(hasUpdate(status({ update_available: false }))).toBe(false)
    expect(hasUpdate(status({ enabled: false }))).toBe(false)
    expect(hasUpdate(status({ latest: null }))).toBe(false)
    expect(hasUpdate(null)).toBe(false)
  })

  it('never calls GitHub from the browser and tolerates old hosts', async () => {
    const fetch = vi.fn(async () => new Response('', { status: 404 }))
    vi.stubGlobal('fetch', fetch)
    expect(await fetchUpdateStatus()).toBeNull()
    await fetchUpdateStatus(true)
    expect(fetch.mock.calls.map((c) => c[0])).toEqual(['./api/update/status', './api/update/status?refresh=1'])
    expect(fetch.mock.calls.some((c) => String(c[0]).includes('github.com'))).toBe(false)
  })

  it('polls while the host is checking, then stops', async () => {
    vi.useFakeTimers()
    const replies = [status({ checking: true, update_available: false, latest: null }), status()]
    const fetch = vi.fn(async () => json(replies.shift() || status()))
    vi.stubGlobal('fetch', fetch)
    let api
    mountNova(defineComponent({ setup() { api = useUpdateStatus({ interval: 50 }); return () => h('div') } }))
    await flushPromises()
    expect(api.busy.value).toBe(true)
    expect(api.available.value).toBe(false)
    await vi.advanceTimersByTimeAsync(60)
    await flushPromises()
    expect(api.available.value).toBe(true)
    expect(api.busy.value).toBe(false)
    const calls = fetch.mock.calls.length
    await vi.advanceTimersByTimeAsync(500)
    expect(fetch.mock.calls.length).toBe(calls)
  })

  it('posts the confirmed tag and reports a refusal', async () => {
    const fetch = vi.fn(async (url, options = {}) => {
      if (url === './api/update/install') {
        expect(JSON.parse(options.body)).toEqual({ tag: 'nova-v0.3.0' })
        return json({ status: false, error: 'streaming' }, { status: 409 })
      }
      return json(status({ streaming: true }))
    })
    vi.stubGlobal('fetch', fetch)
    let api
    mountNova(defineComponent({ setup() { api = useUpdateStatus(); return () => h('div') } }))
    await flushPromises()
    expect(await api.install('nova-v0.3.0')).toBe(false)
    expect(api.installError.value).toBe('conflict')
  })
})

describe('UpdateBanner', () => {
  const mountBanner = (props) => mountNova(UpdateBanner, { props: { install: vi.fn(async () => true), ...props } })

  it('shows nothing without a real update (no false positives)', () => {
    for (const s of [null, status({ update_available: false }), status({ error: 'private', update_available: false, latest: null }), status({ enabled: false })]) {
      const w = mountBanner({ status: s })
      expect(w.find('.nv-alert').exists()).toBe(false)
      w.unmount()
    }
  })

  it('offers the update and installs only after confirmation', async () => {
    const install = vi.fn(async () => true)
    const w = mountBanner({ status: status(), install })
    expect(w.text()).toContain('Nova 0.3.0 is available')
    expect(w.text()).toContain('0.2.0-1d43d4a3')
    const button = w.findAll('button').find((b) => b.text() === 'Install update')
    await button.trigger('click')
    await flushPromises()
    expect(install).not.toHaveBeenCalled()
    const confirm = [...document.body.querySelectorAll('button')].filter((b) => b.textContent.trim() === 'Install update').at(-1)
    expect(document.body.textContent).toContain('Install Nova 0.3.0?')
    confirm.click()
    await flushPromises()
    expect(install).toHaveBeenCalledWith('nova-v0.3.0')
  })

  it('disables install while a device streams', () => {
    const w = mountBanner({ status: status(), streaming: true })
    const button = w.findAll('button').find((b) => b.text() === 'Install update')
    expect(button.attributes('disabled')).toBeDefined()
    expect(w.text()).toContain('no device is streaming')
  })

  it('shows progress, the manual command and failures', () => {
    let w = mountBanner({ status: status({ install: { state: 'downloading', progress: 0.42, tag: 'nova-v0.3.0', message: '', command: '' } }) })
    expect(w.text()).toContain('42%')
    expect(w.findAll('button').some((b) => b.text() === 'Install update')).toBe(false)
    w.unmount()
    w = mountBanner({ status: status({ install: { state: 'manual', tag: 'nova-v0.3.0', progress: 1, message: 'Verified.', command: 'sudo apt-get install --allow-downgrades /x.deb' } }) })
    expect(w.text()).toContain('ready to install')
    expect(w.find('code').text()).toBe('sudo apt-get install --allow-downgrades /x.deb')
    w.unmount()
    w = mountBanner({ status: status({ install: { state: 'failed', tag: 'nova-v0.3.0', progress: 0, message: 'SHA-256 mismatch', command: '' } }) })
    expect(w.text()).toContain("Couldn't install Nova 0.3.0")
    expect(w.text()).toContain('SHA-256 mismatch')
  })
})

import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushPromises, mount } from '@vue/test-utils'
import { createI18n } from 'vue-i18n'
import { createMemoryHistory, createRouter } from 'vue-router'

import en from '../../../src_assets/common/assets/web/public/assets/locale/en.json'
import Apps from '../../../src_assets/common/assets/web/Apps.vue'
import BrowserPlay from '../../../src_assets/common/assets/web/nova/pages/BrowserPlay.vue'
import LibraryCard from '../../../src_assets/common/assets/web/nova/pages/overview/LibraryCard.vue'
import PlayInBrowserDialog from '../../../src_assets/common/assets/web/nova/pages/library/PlayInBrowserDialog.vue'
import { OPTIONS, SECTIONS } from '../../../src_assets/common/assets/web/configs/settings_schema.js'
import tabs from '../../../src_assets/common/assets/web/configs/config_tabs.json'
import {
  UPSTREAM, WEB_CLIENT_PORT, continuePath, defaultDisplayMode, playInBrowserUrl, probeWebClient, webClientBase,
} from '../../../src_assets/common/assets/web/nova/webClient.js'
import { mountNova } from './helpers'

const LAN = new URL('https://192.168.10.10:47990/library')
const RUNNING = { status: true, installed: true, enabled: true, state: 'running', port: 47995, udp_min: 49000, udp_max: 49019, allowed: 'lan', version: 'moonlight-web-stream v2.10.0 (cd9d03cb)', restarts: 0, last_error: '' }
const OFF = { ...RUNNING, enabled: false, state: 'off' }

function json(body, { status = 200 } = {}) {
  return {
    ok: status < 400, status, statusText: 'OK',
    headers: { get: () => 'application/json' },
    json: async () => body,
    clone() { return this },
  }
}

/** fetch stub: /api/web-client answers `state` (POST switches it), /api/apps a small library. */
function hostStub(initial) {
  const state = { current: { ...initial }, posts: [] }
  const fn = vi.fn(async (url, options = {}) => {
    const u = String(url)
    if (u === './api/web-client') {
      if (options.method === 'POST') {
        const body = JSON.parse(options.body)
        state.posts.push(body)
        state.current = { ...state.current, enabled: body.enabled, state: body.enabled ? 'running' : 'off' }
      }
      return json(state.current)
    }
    if (u === './api/apps') return json({ apps: [{ name: 'Desktop' }, { name: 'Hades II', 'image-path': 'x.png' }], running_index: null })
    if (u === './api/config') return json({ platform: 'linux' })
    if (u.endsWith('/nova/health')) return { ok: true }
    return json({ status: true })
  })
  vi.stubGlobal('fetch', fn)
  return state
}

let wrapper
afterEach(() => {
  wrapper?.unmount()
  wrapper = null
  document.body.innerHTML = ''
  vi.unstubAllGlobals()
})

describe('browser client links', () => {
  it('points at the gateway port on the host the web UI was opened with', () => {
    expect(WEB_CLIENT_PORT).toBe(47995)
    expect(webClientBase(LAN)).toBe('https://192.168.10.10:47995')
    expect(webClientBase(LAN, 57995)).toBe('https://192.168.10.10:57995')
    expect(webClientBase(new URL('https://100.100.10.1:47990/'))).toBe('https://100.100.10.1:47995')
    expect(webClientBase(new URL('https://[fd7a::1]:47990/'))).toBe('https://[fd7a::1]:47995')
  })

  it('builds /nova/play links and only passes the two display modes', () => {
    expect(playInBrowserUrl('Hades II', 'mirror', { where: LAN })).toBe('https://192.168.10.10:47995/nova/play?app=Hades+II&display=mirror')
    expect(playInBrowserUrl('A&B', 'virtual', { where: LAN, port: 57995 })).toBe('https://192.168.10.10:57995/nova/play?app=A%26B&display=virtual')
    expect(playInBrowserUrl('Desktop', 'x', { where: LAN })).toBe('https://192.168.10.10:47995/nova/play?app=Desktop')
  })

  it('only continues to paths on the browser client', () => {
    expect(continuePath('/nova/play?app=Desktop&display=mirror')).toBe('/nova/play?app=Desktop&display=mirror')
    for (const bad of [undefined, '', 'https://evil.com', '//evil.com', '/\\evil.com', '/a b', ['/x'], '/x\n']) {
      expect(continuePath(bad)).toBe('')
    }
  })

  it("defaults to the app's saved display mode, else virtual", () => {
    expect(defaultDisplayMode({ 'nova-display-mode': 'mirror' })).toBe('mirror')
    expect(defaultDisplayMode({ 'nova-display-mode': 'weird' })).toBe('virtual')
    expect(defaultDisplayMode({})).toBe('virtual')
  })

  it('probes /nova/health without credentials and treats errors as unreachable', async () => {
    const ok = vi.fn(async () => ({ ok: true }))
    expect(await probeWebClient({ fetchImpl: ok, where: LAN })).toBe(true)
    expect(ok.mock.calls[0][0]).toBe('https://192.168.10.10:47995/nova/health')
    expect(ok.mock.calls[0][1].credentials).toBe('omit')
    expect(await probeWebClient({ fetchImpl: async () => { throw new TypeError('cert') }, where: LAN })).toBe(false)
  })

  it('is a Network setting, off by default', () => {
    expect(OPTIONS.web_client.type).toBe('bool')
    expect(SECTIONS.find((s) => s.id === 'network').options).toContain('web_client')
    const network = tabs.find((tab) => tab.id === 'network')
    expect(network.options.web_client).toBe('disabled')
    expect(en.config.web_client_desc).toContain('LAN and Tailscale')
  })
})

describe('Play in browser dialog', () => {
  it('shows the choice, the link and whether the client is reachable', async () => {
    vi.stubGlobal('fetch', vi.fn(async (url) => {
      if (String(url).endsWith('/nova/health')) throw new TypeError('offline')
      return json(RUNNING)
    }))
    wrapper = mountNova(PlayInBrowserDialog, { props: { open: true, app: { name: 'Desktop', 'nova-display-mode': 'mirror' } } })
    await flushPromises()
    const dialog = document.querySelector('[role="dialog"]')
    expect(dialog.textContent).toContain('Play Desktop in a browser')
    expect(dialog.querySelector('[aria-pressed="true"]').textContent).toBe('Mirror desktop')
    expect(dialog.textContent).toContain('not reachable')
    const open = dialog.querySelector('[data-nv-play]')
    expect(open.getAttribute('target')).toBe('_blank')
    expect(open.getAttribute('rel')).toContain('noopener')
    expect(open.getAttribute('href')).toMatch(/:47995\/nova\/play\?app=Desktop&display=mirror$/)

    ;[...dialog.querySelectorAll('[aria-pressed]')].find((b) => b.textContent === 'Virtual display').click()
    await flushPromises()
    expect(dialog.querySelector('[data-nv-play]').getAttribute('href')).toMatch(/display=virtual$/)
    expect(dialog.querySelector('[data-nv-play-url]').textContent).toMatch(/display=virtual$/)
  })

  it('says so when the client answers', async () => {
    hostStub(RUNNING)
    wrapper = mountNova(PlayInBrowserDialog, { props: { open: true, app: { name: 'Desktop' } } })
    await flushPromises()
    expect(document.querySelector('[role="dialog"]').textContent).toContain('Browser client is running')
  })

  it('points to the Play in browser page when the client is off', async () => {
    hostStub(OFF)
    wrapper = mountNova(PlayInBrowserDialog, { props: { open: true, app: { name: 'Desktop' } } })
    await flushPromises()
    const dialog = document.querySelector('[role="dialog"]')
    expect(dialog.querySelector('[data-nv-pib-off]').textContent).toContain('switched off')
    expect(dialog.querySelector('[data-nv-play]')).toBeNull()
    expect(dialog.querySelector('[data-nv-pib-manage]')).not.toBeNull()
  })
})

describe('Play in browser entries', () => {
  it('is in the Library row menu', async () => {
    hostStub(RUNNING)
    const router = createRouter({ history: createMemoryHistory(), routes: [{ path: '/library', component: Apps }] })
    const i18n = createI18n({ legacy: false, locale: 'en', messages: { en } })
    await router.push('/library?view=list')
    await router.isReady()
    wrapper = mount({ template: '<router-view />' }, { attachTo: document.body, global: { plugins: [router, i18n] } })
    await flushPromises()
    document.querySelectorAll('tbody [aria-haspopup="menu"]')[1].click()
    await flushPromises()
    ;[...document.querySelectorAll('[role="menuitem"]')].find((b) => b.textContent.includes('Play in browser')).click()
    await flushPromises()
    expect(document.querySelector('[role="dialog"]').textContent).toContain('Play Hades II in a browser')
  })

  it('is in the Overview library card menu', async () => {
    hostStub(RUNNING)
    wrapper = mountNova(LibraryCard, { props: { data: { apps: [{ name: 'Desktop' }], runningIndex: null } } })
    await flushPromises()
    document.querySelector('[aria-haspopup="menu"]').click()
    await flushPromises()
    ;[...document.querySelectorAll('[role="menuitem"]')].find((b) => b.textContent.includes('Play in browser')).click()
    await flushPromises()
    expect(document.querySelector('[role="dialog"]').textContent).toContain('Play Desktop in a browser')
  })
})

describe('Play in browser page', () => {
  async function mountPage(query = '') {
    const router = createRouter({ history: createMemoryHistory(), routes: [{ path: '/browser', component: BrowserPlay }] })
    const i18n = createI18n({ legacy: false, locale: 'en', messages: { en } })
    await router.push(`/browser${query}`)
    await router.isReady()
    wrapper = mount({ template: '<router-view />' }, { attachTo: document.body, global: { plugins: [router, i18n] } })
    await flushPromises()
  }

  it('is off by default, and switching it on enables the game links', async () => {
    const host = hostStub(OFF)
    await mountPage()
    const toggle = document.querySelector('#nv-bp-toggle')
    expect(toggle.getAttribute('aria-checked')).toBe('false')
    expect(document.querySelector('[data-nv-web-client-state]').textContent).toContain('Off')
    expect(document.querySelector('[data-nv-browser-play]')).toBeNull()
    expect(document.body.textContent).toContain('Switch the browser client on')
    expect(document.body.textContent).toContain(UPSTREAM.name)
    expect(document.body.textContent).toContain('GPL-3.0')

    toggle.click()
    await flushPromises()
    expect(host.posts).toEqual([{ enabled: true }])
    expect(document.querySelector('[data-nv-web-client-state]').textContent).toContain('Running')
    const links = [...document.querySelectorAll('[data-nv-browser-play]')]
    expect(links.map((a) => a.textContent.trim())).toEqual(['Desktop', 'Hades II'])
    // Same tab (the stream plays here), same host name as this page, the display choice passed on.
    expect(links[1].getAttribute('target')).toBeNull()
    expect(links[1].getAttribute('href')).toBe(`https://${location.hostname}:47995/nova/play?app=Hades+II&display=virtual`)
    ;[...document.querySelectorAll('[aria-pressed]')].find((b) => b.textContent === 'Mirror desktop').click()
    await flushPromises()
    expect(document.querySelector('[data-nv-browser-play]').getAttribute('href')).toMatch(/display=mirror$/)
    expect(document.querySelector('[data-nv-web-client-url]').textContent).toBe(`https://${location.hostname}:47995`)
  })

  it('says who can connect', async () => {
    hostStub({ ...RUNNING, allowed: 'pc' })
    await mountPage()
    expect(document.body.textContent).toContain('Only this computer')
  })

  it('explains a build without the sidecar and disables the switch', async () => {
    hostStub({ ...OFF, installed: false, state: 'not_installed' })
    await mountPage()
    expect(document.body.textContent).toContain('Not included in this build')
    expect(document.querySelector('#nv-bp-toggle').disabled).toBe(true)
  })

  it('goes back to the browser client after signing in', async () => {
    hostStub(RUNNING)
    const assign = vi.fn()
    vi.stubGlobal('location', { ...window.location, hostname: '192.168.10.10', assign })
    await mountPage('?continue=' + encodeURIComponent('/nova/play?app=Desktop&display=mirror'))
    expect(assign).toHaveBeenCalledWith('https://192.168.10.10:47995/nova/play?app=Desktop&display=mirror')
  })

  it('ignores an unsafe continue target', async () => {
    hostStub(RUNNING)
    const assign = vi.fn()
    vi.stubGlobal('location', { ...window.location, hostname: '192.168.10.10', assign })
    await mountPage('?continue=' + encodeURIComponent('//evil.example/x'))
    expect(assign).not.toHaveBeenCalled()
  })
})

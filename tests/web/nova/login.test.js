import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { flushPromises } from '@vue/test-utils'

import { mountNova } from './helpers.js'
import Login from '../../../src_assets/common/assets/web/Login.vue'
import { apiFetch, resetBuildCheck } from '../../../src_assets/common/assets/web/fetch_utils.js'
import { logout } from '../../../src_assets/common/assets/web/nova/api.js'
import {
  csrfToken, loginUrl, nav, redirectToLogin, resetAuthState, safeNext, setCsrfToken,
} from '../../../src_assets/common/assets/web/nova/auth.js'

const wrappers = []
const track = (w) => { wrappers.push(w); return w }
const realNav = { ...nav }

/** A minimal fetch Response. */
function reply(status, body, headers = {}) {
  const h = new Headers(headers)
  return { ok: status >= 200 && status < 300, status, statusText: '', headers: h, json: async () => body, clone() { return this } }
}

/** fetch stub routing by URL; unknown URLs answer 404. */
function stubFetch(routes) {
  const fn = vi.fn(async (url, options = {}) => {
    const key = `${(options.method || 'GET').toUpperCase()} ${url}`
    const handler = routes[key] ?? routes[url]
    if (!handler) return reply(404, {})
    return typeof handler === 'function' ? handler(url, options) : handler
  })
  vi.stubGlobal('fetch', fn)
  return fn
}

beforeEach(() => {
  resetAuthState()
  resetBuildCheck()
  nav.assign = vi.fn()
  nav.replace = vi.fn()
  globalThis.history.replaceState(null, '', '/')
})

afterEach(() => {
  while (wrappers.length) wrappers.pop().unmount()
  document.body.innerHTML = ''
  Object.assign(nav, realNav)
  vi.restoreAllMocks()
  vi.unstubAllGlobals()
  vi.useRealTimers()
})

const byLabel = (w, text) => {
  const label = w.findAll('label').find((l) => l.text() === text)
  return w.get(`#${label.attributes('for')}`)
}

async function mountLogin(query = {}, routes = {}) {
  const fetchMock = stubFetch({ 'GET /api/auth/session': reply(200, { authenticated: false, setup_required: false }), ...routes })
  const w = track(mountNova(Login))
  await w.vm.$router.replace({ path: '/login', query })
  await flushPromises()
  return { w, fetchMock }
}

async function fillAndSubmit(w, { username = 'ayush', password = 'secret', remember = false } = {}) {
  await byLabel(w, 'Username').setValue(username)
  await byLabel(w, 'Password').setValue(password)
  if (remember) await byLabel(w, 'Keep me signed in').setValue(true)
  await w.get('form').trigger('submit')
  await flushPromises()
}

describe('safeNext (open-redirect guard)', () => {
  it('accepts same-origin paths', () => {
    for (const ok of ['/', '/settings', '/settings?tab=a#b', '/devices/abc']) expect(safeNext(ok)).toBe(ok)
  })

  it('rejects absolute, scheme-relative and odd destinations', () => {
    for (const bad of [
      undefined, null, '', 'https://evil.example', '//evil.example', '/\\evil.example', '\\\\evil', 'evil.example',
      'javascript:alert(1)', '/a\nb', '/x\\y', '/login', '/login?next=/x', '/logout', '/welcome', ['/settings'], `/${'a'.repeat(3000)}`,
    ]) {
      expect(safeNext(bad)).toBeNull()
    }
  })

  it('builds /login?next= from the current route', () => {
    globalThis.history.replaceState(null, '', '/settings?tab=network#video')
    expect(loginUrl()).toBe(`/login?next=${encodeURIComponent('/settings?tab=network#video')}`)
    expect(loginUrl('/')).toBe('/login')
    expect(loginUrl('//evil.example')).toBe('/login')
  })
})

describe('Login page', () => {
  it('shows username, password and "Keep me signed in" with password-manager hints', async () => {
    const { w } = await mountLogin()
    expect(w.get('h1').text()).toBe('Sign in')
    expect(byLabel(w, 'Username').attributes('autocomplete')).toBe('username')
    expect(byLabel(w, 'Password').attributes('autocomplete')).toBe('current-password')
    expect(byLabel(w, 'Password').attributes('type')).toBe('password')
    expect(byLabel(w, 'Keep me signed in').element.checked).toBe(false)
    expect(w.text()).toContain('For 30 days on this browser.')
  })

  it('validates empty fields without asking the host', async () => {
    const { w, fetchMock } = await mountLogin()
    await w.get('form').trigger('submit')
    await flushPromises()
    expect(w.text()).toContain('Enter a username.')
    expect(w.text()).toContain('Enter your password.')
    expect(fetchMock.mock.calls.some(([url]) => url === '/api/auth/login')).toBe(false)
  })

  it('signs in, remembers the choice, and returns to ?next=', async () => {
    const { w, fetchMock } = await mountLogin({ next: '/settings?tab=network' }, {
      'POST /api/auth/login': reply(200, { status: true, csrf_token: 'c'.repeat(64) }),
    })
    await fillAndSubmit(w, { username: ' ayush ', password: 'secret', remember: true })
    const call = fetchMock.mock.calls.find(([url]) => url === '/api/auth/login')
    expect(JSON.parse(call[1].body)).toEqual({ username: 'ayush', password: 'secret', remember: true })
    expect(call[1].credentials).toBe('same-origin')
    expect(nav.replace).toHaveBeenCalledWith('/settings?tab=network')
    expect(csrfToken()).toBe('c'.repeat(64))
  })

  it('never follows an off-site ?next=', async () => {
    const { w } = await mountLogin({ next: '//evil.example/steal' }, {
      'POST /api/auth/login': reply(200, { status: true, csrf_token: 'x' }),
    })
    await fillAndSubmit(w)
    expect(nav.replace).toHaveBeenCalledWith('/')
  })

  it('shows an error for a wrong password and clears the field', async () => {
    const { w } = await mountLogin({}, { 'POST /api/auth/login': reply(401, { status: false, error: 'Wrong username or password' }) })
    await fillAndSubmit(w)
    expect(w.get('[role="alert"]').text()).toContain('don’t match')
    expect(byLabel(w, 'Password').element.value).toBe('')
    expect(nav.replace).not.toHaveBeenCalled()
  })

  it('counts down a lockout (429 + Retry-After) and re-enables the button', async () => {
    vi.useFakeTimers({ toFake: ['setInterval', 'clearInterval', 'Date'] })
    const { w } = await mountLogin({}, {
      'POST /api/auth/login': reply(429, { status: false, retry_after: 30 }, { 'Retry-After': '30' }),
    })
    await fillAndSubmit(w)
    expect(w.text()).toContain('Too many attempts')
    expect(w.text()).toContain('Sign-in is paused for 0:30')
    const button = w.get('button[type="submit"]')
    expect(button.attributes('disabled')).toBeDefined()
    expect(button.text()).toContain('Try again in 0:30')
    vi.advanceTimersByTime(5000)
    await flushPromises()
    expect(button.text()).toContain('Try again in 0:25')
    vi.advanceTimersByTime(26000)
    await flushPromises()
    expect(button.attributes('disabled')).toBeUndefined()
    expect(button.text()).toBe('Sign in')
  })

  it('goes straight on when already signed in, and to setup before the first account', async () => {
    const signedIn = await mountLogin({ next: '/library' }, { 'GET /api/auth/session': reply(200, { authenticated: true, csrf_token: 't' }) })
    expect(nav.replace).toHaveBeenCalledWith('/library')
    signedIn.w.unmount()
    wrappers.pop()

    const setup = await mountLogin({}, { 'GET /api/auth/session': reply(200, { authenticated: false, setup_required: true }) })
    expect(setup.w.vm.$route.path).toBe('/welcome')
  })

  it('confirms signing out', async () => {
    const { w } = await mountLogin({ signed_out: '1' })
    expect(w.text()).toContain('You’re signed out.')
  })
})

describe('apiFetch and sessions', () => {
  it('sends the page to /login on 401, keeping the route', async () => {
    globalThis.history.replaceState(null, '', '/devices/abc?tab=perm')
    stubFetch({ 'GET ./api/clients/list': reply(401, { status: false }) })
    const response = await apiFetch('./api/clients/list')
    expect(response.status).toBe(401)
    expect(nav.assign).toHaveBeenCalledWith(`/login?next=${encodeURIComponent('/devices/abc?tab=perm')}`)
    // Several failing requests redirect once.
    await apiFetch('./api/clients/list')
    expect(nav.assign).toHaveBeenCalledTimes(1)
  })

  it('does not redirect from the sign-in page itself', () => {
    globalThis.history.replaceState(null, '', '/login')
    expect(redirectToLogin()).toBe(false)
    expect(nav.assign).not.toHaveBeenCalled()
  })

  it('marks requests as XHR and sends the CSRF token on state changes only', async () => {
    setCsrfToken('tok')
    const fetchMock = stubFetch({ './api/config': reply(200, {}) })
    await apiFetch('./api/config')
    await apiFetch('./api/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}' })
    const [get, post] = fetchMock.mock.calls.map(([, options]) => options)
    expect(get.headers['X-Requested-With']).toBe('XMLHttpRequest')
    expect(get.headers['X-CSRF-Token']).toBeUndefined()
    expect(post.headers['X-CSRF-Token']).toBe('tok')
    expect(post.headers['Content-Type']).toBe('application/json')
  })

  it('refreshes a stale CSRF token and retries once', async () => {
    setCsrfToken('old')
    let posts = 0
    const fetchMock = stubFetch({
      'POST ./api/apps': (url, options) => (++posts === 1 ? reply(400, { error: 'Invalid CSRF token' }) : reply(200, { ok: options.headers['X-CSRF-Token'] })),
      'GET /api/auth/session': reply(200, { authenticated: true, csrf_token: 'new' }),
    })
    const response = await apiFetch('./api/apps', { method: 'POST', body: '{}' })
    expect(response.status).toBe(200)
    expect(await response.json()).toEqual({ ok: 'new' })
    expect(fetchMock).toHaveBeenCalledTimes(3)
  })

  it('signs out on the host, then shows the sign-in page', async () => {
    setCsrfToken('tok')
    const fetchMock = stubFetch({ 'POST /api/auth/logout': reply(200, { status: true }) })
    await logout()
    const [url, options] = fetchMock.mock.calls[0]
    expect(url).toBe('/api/auth/logout')
    expect(options.method).toBe('POST')
    expect(options.headers['X-CSRF-Token']).toBe('tok')
    expect(nav.replace).toHaveBeenCalledWith('/login?signed_out=1')
    expect(csrfToken()).toBe('')
  })

  it('still leaves the page when the host is unreachable', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => { throw new TypeError('Failed to fetch') }))
    await logout()
    expect(nav.replace).toHaveBeenCalledWith('/login?signed_out=1')
  })
})

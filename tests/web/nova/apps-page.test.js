import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { mount, flushPromises } from '@vue/test-utils'
import { createI18n } from 'vue-i18n'
import { createMemoryHistory, createRouter } from 'vue-router'

import en from '../../../src_assets/common/assets/web/public/assets/locale/en.json'
import Apps from '../../../src_assets/common/assets/web/Apps.vue'

const APPS = [{ name: 'Desktop' }, { name: 'Steam', cmd: 'steam' }, { name: 'Game', cmd: 'lutris' }]

let wrapper
let calls

async function mountAt(path, apps = APPS) {
  calls = []
  vi.stubGlobal('fetch', vi.fn(async (url, options = {}) => {
    calls.push({ url, method: options.method || 'GET', body: options.body })
    const body = url === './api/apps' && !options.method ? { apps } : url === './api/config' ? { platform: 'linux' } : { status: true }
    return { ok: true, status: 200, statusText: 'OK', json: async () => body, clone() { return this } }
  }))
  const router = createRouter({ history: createMemoryHistory(), routes: [{ path: '/apps', component: Apps }, { path: '/', component: { template: '<div />' } }] })
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
  document.body.innerHTML = ''
  vi.unstubAllGlobals()
})

describe('Applications page', () => {
  it('has a single primary action in the header', async () => {
    await mountAt('/apps')
    const header = document.querySelector('.nv-apps-add')
    expect(header.textContent).toContain('Add application')
    expect(document.body.textContent).not.toMatch(/Close running app(?!…)/)
  })

  it('hides search below 8 apps and filters from ?q=', async () => {
    await mountAt('/apps')
    expect(document.querySelector('input[type="search"]')).toBeNull()
    const many = Array.from({ length: 9 }, (_, i) => ({ name: `App ${i}` }))
    wrapper.unmount()
    await mountAt('/apps?q=App%203', many)
    expect(document.querySelector('input[type="search"]').value).toBe('App 3')
    expect(document.querySelectorAll('.nv-app')).toHaveLength(1)
  })

  it('shows "No matches" with Clear, which resets the URL', async () => {
    const many = Array.from({ length: 9 }, (_, i) => ({ name: `App ${i}` }))
    const router = await mountAt('/apps?q=zzz', many)
    expect(document.body.textContent).toContain('No matches for “zzz”')
    const clear = [...document.querySelectorAll('button')].find((b) => b.textContent.includes('Clear search'))
    clear.click()
    await flushPromises()
    expect(router.currentRoute.value.query.q).toBeUndefined()
    expect(document.querySelectorAll('.nv-app')).toHaveLength(9)
  })

  it('opens the editor from ?edit= and Back closes it', async () => {
    const router = await mountAt('/apps')
    document.getElementById('nv-app-1').click()
    await flushPromises()
    expect(router.currentRoute.value.query.edit).toBe('1')
    expect(document.getElementById('nv-app-name').value).toBe('Steam')
    router.back()
    await flushPromises()
    await new Promise((r) => setTimeout(r, 0))
    await flushPromises()
    expect(router.currentRoute.value.query.edit).toBeUndefined()
    expect(document.querySelector('.nv-sheet')).toBeNull()
  })

  it('guards unsaved edits against navigation', async () => {
    const router = await mountAt('/apps?edit=new')
    const name = document.getElementById('nv-app-name')
    name.value = 'Something'
    name.dispatchEvent(new Event('input'))
    await flushPromises()
    await router.push('/')
    await flushPromises()
    expect(router.currentRoute.value.path).toBe('/apps')
    expect(document.body.textContent).toContain('Discard your changes?')
    const discard = [...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'Discard')
    discard.click()
    await flushPromises()
    expect(router.currentRoute.value.path).toBe('/')
  })

  it('deletes through a confirm that names the app, with Cancel focused', async () => {
    await mountAt('/apps')
    const more = document.querySelectorAll('.nv-app [aria-haspopup="menu"]')[2]
    more.click()
    await flushPromises()
    ;[...document.querySelectorAll('[role="menuitem"]')].find((b) => b.textContent.includes('Delete')).click()
    await flushPromises()
    expect(document.body.textContent).toContain('Delete Game?')
    expect(document.activeElement.textContent.trim()).toBe('Cancel')
    ;[...document.querySelectorAll('[role="dialog"] button')].find((b) => b.textContent.trim() === 'Delete').click()
    await flushPromises()
    expect(calls.some((c) => c.url === './api/apps/2' && c.method === 'DELETE')).toBe(true)
  })

  it('shows a named error with Retry when the list fails to load', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: false, status: 500, statusText: 'Error', json: async () => ({}), clone() { return this } })))
    const router = createRouter({ history: createMemoryHistory(), routes: [{ path: '/apps', component: Apps }] })
    await router.push('/apps')
    wrapper = mount({ template: '<router-view />' }, { attachTo: document.body, global: { plugins: [router, createI18n({ legacy: false, locale: 'en', messages: { en } })] } })
    await flushPromises()
    expect(document.body.textContent).toContain('Couldn’t load applications')
    expect([...document.querySelectorAll('button')].some((b) => b.textContent.includes('Try again'))).toBe(true)
  })
})

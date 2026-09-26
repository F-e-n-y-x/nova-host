import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { flushPromises, mount } from '@vue/test-utils'
import { createI18n } from 'vue-i18n'
import { createMemoryHistory, createRouter } from 'vue-router'

import en from '../../../src_assets/common/assets/web/public/assets/locale/en.json'
import Devices from '../../../src_assets/common/assets/web/nova/pages/Devices.vue'
import PermissionEditor from '../../../src_assets/common/assets/web/nova/devices/PermissionEditor.vue'
import { toasts } from '../../../src_assets/common/assets/web/nova/toast.js'
import {
  filterDevices, isValidDeviceName, permissionPreset, relativeTime, shortId, sortDevices,
} from '../../../src_assets/common/assets/web/nova/devices/format.js'
import { mountNova } from './helpers.js'

const FULL = { input_keyboard: true, input_mouse: true, input_controller: true, input_touch_pen: true, clipboard: true, launch_apps: true, preset: 'full' }

function device(overrides = {}) {
  return { name: 'Pixel 9 Pro', uuid: 'AAAAAAAA-1111-2222-3333-BBBBBBBBBBBB', enabled: true, permissions: FULL, paired_at: 1790000000, last_connected_at: null, connected: false, ...overrides }
}

/**
 * Stub fetch with per-route handlers.
 *
 * @param {Record<string, (body: any, init: RequestInit) => [number, any]>} routes "METHOD path" → handler.
 * @returns {import('vitest').Mock} The stub, whose calls hold [url, init].
 */
function stubHost(routes) {
  const stub = vi.fn(async (url, init = {}) => {
    const path = new URL(url, 'https://host/').pathname
    const key = `${(init.method || 'GET').toUpperCase()} ${path}`
    const handler = routes[key]
    if (!handler) return new Response(JSON.stringify({ error: `no route ${key}` }), { status: 404 })
    const [status, body] = handler(init.body ? JSON.parse(init.body) : undefined, init)
    return new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json' } })
  })
  vi.stubGlobal('fetch', stub)
  return stub
}

async function mountDevices(path = '/devices') {
  const i18n = createI18n({ legacy: false, locale: 'en', fallbackLocale: 'en', messages: { en } })
  const router = createRouter({
    history: createMemoryHistory(),
    routes: [
      { path: '/devices/:uuid?', component: Devices },
      { path: '/:pathMatch(.*)*', component: { template: '<div />' } },
    ],
  })
  router.push(path)
  await router.isReady()
  const wrapper = mount({ template: '<router-view />' }, { attachTo: document.body, global: { plugins: [i18n, router] } })
  await flushPromises()
  return { wrapper, router }
}

function calls(stub, key) {
  return stub.mock.calls.filter(([url, init = {}]) => `${(init.method || 'GET').toUpperCase()} ${new URL(url, 'https://host/').pathname}` === key)
}

beforeEach(() => {
  toasts.splice(0)
  document.body.innerHTML = ''
})

afterEach(() => {
  vi.unstubAllGlobals()
})

describe('device formatting', () => {
  it('shortens IDs, formats times and validates names', () => {
    expect(shortId('AAAAAAAA-1111-2222-3333-BBBBBBBBBBBB')).toBe('AAAAAAAA…BBBB')
    expect(shortId('short')).toBe('short')
    expect(relativeTime(null, 'en')).toBe('')
    expect(relativeTime(1000 - 3 * 3600, 'en', 1000 * 1000)).toBe('3 hours ago')
    expect(isValidDeviceName('Living room TV')).toBe(true)
    expect(isValidDeviceName(' padded')).toBe(false)
    expect(isValidDeviceName('a'.repeat(65))).toBe(false)
    expect(isValidDeviceName('tab\tname')).toBe(false)
    expect(permissionPreset(undefined)).toBe('full')
    expect(permissionPreset({ preset: 'weird' })).toBe('custom')
  })

  it('filters by name or ID and sorts streaming, then recent, then by name', () => {
    const list = [device({ name: 'b', uuid: '1' }), device({ name: 'a', uuid: '2', last_connected_at: 5 }), device({ name: 'c', uuid: '3', connected: true })]
    expect(sortDevices(list).map((d) => d.uuid)).toEqual(['3', '2', '1'])
    expect(filterDevices(list, 'B').map((d) => d.uuid)).toEqual(['1'])
    expect(filterDevices(list, '3').map((d) => d.uuid)).toEqual(['3'])
  })
})

describe('PermissionEditor', () => {
  it('sends a preset or a single flag with the resulting permissions', async () => {
    const wrapper = mountNova(PermissionEditor, { props: { permissions: FULL, deviceName: 'Phone' } })
    const buttons = wrapper.findAll('.nv-seg__option')
    expect(buttons.map((b) => b.text())).toEqual(['Full access', 'Play', 'View only'])
    expect(buttons[0].attributes('aria-pressed')).toBe('true')

    await buttons[2].trigger('click')
    const [update, next] = wrapper.emitted('change')[0]
    expect(update).toEqual({ preset: 'view_only' })
    expect(next.preset).toBe('view_only')
    expect(next.input_keyboard).toBe(false)

    const switches = wrapper.findAll('[role="switch"]')
    expect(switches).toHaveLength(6)
    await switches[4].trigger('click') // clipboard off → matches "play"
    const [update2, next2] = wrapper.emitted('change')[1]
    expect(update2).toEqual({ clipboard: false })
    expect(next2.preset).toBe('play')
    wrapper.unmount()
  })
})

describe('Devices page', () => {
  it('shows an empty state with a pair action when nothing is paired', async () => {
    stubHost({ 'GET /api/clients/list': () => [200, { status: true, named_certs: [] }], 'GET /api/pin': () => [200, { pairings: [] }] })
    const { wrapper } = await mountDevices()
    expect(wrapper.text()).toContain('No devices paired yet')
    expect(wrapper.text()).not.toContain('Unpair all devices')
    wrapper.unmount()
  })

  it('names what failed and retries', async () => {
    let fail = true
    stubHost({
      'GET /api/clients/list': () => (fail ? [500, { error: 'boom' }] : [200, { status: true, named_certs: [device()] }]),
      'GET /api/pin': () => [200, { pairings: [] }],
    })
    const { wrapper } = await mountDevices()
    expect(wrapper.text()).toContain('Couldn’t load paired devices')
    fail = false
    await wrapper.findAll('button').find((b) => b.text() === 'Try again').trigger('click')
    await flushPromises()
    expect(wrapper.text()).toContain('Pixel 9 Pro')
    wrapper.unmount()
  })

  it('blocks a device optimistically and rolls back with a named error', async () => {
    const stub = stubHost({
      'GET /api/clients/list': () => [200, { status: true, named_certs: [device()] }],
      'GET /api/pin': () => [200, { pairings: [] }],
      'POST /api/clients/update': () => [400, { status: false, error: 'nope' }],
    })
    const { wrapper } = await mountDevices()
    const toggle = wrapper.find('.nv-device-row [role="switch"]')
    expect(toggle.attributes('aria-checked')).toBe('true')
    await toggle.trigger('click')
    expect(JSON.parse(calls(stub, 'POST /api/clients/update')[0][1].body)).toEqual({ uuid: device().uuid, enabled: false })
    await flushPromises()
    expect(wrapper.find('.nv-device-row [role="switch"]').attributes('aria-checked')).toBe('true')
    expect(toasts.at(-1).variant).toBe('danger')
    expect(toasts.at(-1).message).toContain('Pixel 9 Pro')
    wrapper.unmount()
  })

  it('opens a device at /devices/:uuid, renames it and asks before unpairing', async () => {
    const stub = stubHost({
      'GET /api/clients/list': () => [200, { status: true, named_certs: [device({ connected: true })] }],
      'GET /api/pin': () => [200, { pairings: [] }],
      'POST /api/clients/update': () => [200, { status: true }],
      'POST /api/clients/unpair': () => [200, { status: true }],
      'POST /api/clients/disconnect': () => [200, { status: true }],
    })
    const { wrapper, router } = await mountDevices()
    await wrapper.find('.nv-device-row__name').trigger('click')
    await flushPromises()
    expect(router.currentRoute.value.path).toBe(`/devices/${device().uuid}`)

    const dialog = () => document.querySelector('[role="dialog"]')
    expect(dialog().textContent).toContain('Pixel 9 Pro is streaming')

    const nameInput = dialog().querySelector('input[type="text"]')
    nameInput.value = 'Living room TV'
    nameInput.dispatchEvent(new Event('input'))
    await flushPromises()
    dialog().querySelector('form').dispatchEvent(new Event('submit'))
    await flushPromises()
    expect(JSON.parse(calls(stub, 'POST /api/clients/update')[0][1].body)).toEqual({ uuid: device().uuid, name: 'Living room TV' })

    const endStream = [...dialog().querySelectorAll('button')].find((b) => b.textContent.trim() === 'End stream')
    endStream.click()
    await flushPromises()
    expect(calls(stub, 'POST /api/clients/disconnect')).toHaveLength(1)

    const unpairButton = [...dialog().querySelectorAll('button')].find((b) => b.textContent.trim() === 'Unpair Living room TV')
    unpairButton.click()
    await flushPromises()
    expect(dialog().textContent).toContain('Unpair Living room TV?')
    expect(document.activeElement.textContent.trim()).toBe('Cancel')
    expect(calls(stub, 'POST /api/clients/unpair')).toHaveLength(0)

    const confirm = [...dialog().querySelectorAll('.nv-panel__confirm button')].find((b) => b.textContent.trim() === 'Unpair Living room TV')
    confirm.click()
    await flushPromises()
    expect(calls(stub, 'POST /api/clients/unpair')).toHaveLength(1)
    expect(router.currentRoute.value.path).toBe('/devices')
    expect(wrapper.text()).toContain('No devices paired yet')
    wrapper.unmount()
  })

  it('needs the checkbox before unpairing every device', async () => {
    const stub = stubHost({
      'GET /api/clients/list': () => [200, { status: true, named_certs: [device(), device({ name: 'Deck', uuid: 'deck' })] }],
      'GET /api/pin': () => [200, { pairings: [] }],
      'POST /api/clients/unpair-all': () => [200, { status: true }],
    })
    const { wrapper } = await mountDevices()
    await wrapper.findAll('button').find((b) => b.text() === 'Unpair all devices').trigger('click')
    await flushPromises()
    const dialog = document.querySelector('[role="dialog"]')
    const confirm = [...dialog.querySelectorAll('footer button')].find((b) => b.textContent.trim() === 'Unpair all devices')
    expect(confirm.disabled).toBe(true)
    const check = dialog.querySelector('input[type="checkbox"]')
    check.click()
    await flushPromises()
    expect(confirm.disabled).toBe(false)
    confirm.click()
    await flushPromises()
    expect(calls(stub, 'POST /api/clients/unpair-all')).toHaveLength(1)
    wrapper.unmount()
  })

  it('shows search from 8 devices and keeps the query in the URL', async () => {
    const many = Array.from({ length: 9 }, (_, i) => device({ name: `Device ${i}`, uuid: `uuid-${i}` }))
    stubHost({ 'GET /api/clients/list': () => [200, { status: true, named_certs: many }], 'GET /api/pin': () => [200, { pairings: [] }] })
    const { wrapper, router } = await mountDevices('/devices?q=zzz')
    expect(wrapper.find('[role="search"]').exists()).toBe(true)
    expect(wrapper.text()).toContain('No devices match “zzz”')
    await wrapper.findAll('button').find((b) => b.text() === 'Clear search').trigger('click')
    await flushPromises()
    expect(router.currentRoute.value.query.q).toBeUndefined()
    expect(wrapper.findAll('.nv-device-row')).toHaveLength(9)
    wrapper.unmount()
  })
})

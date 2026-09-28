import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushPromises } from '@vue/test-utils'

import PairForm from '../../../src_assets/common/assets/web/nova/pair/PairForm.vue'
import { mountNova } from './helpers.js'

const REQUEST = { id: '0123456789abcdef0123456789abcdef', name: 'Pixel 9 Pro', address: '192.168.1.20' }

function stubPin(reply) {
  const stub = vi.fn(async () => new Response(JSON.stringify(reply.body), { status: reply.status, headers: { 'Content-Type': 'application/json' } }))
  vi.stubGlobal('fetch', stub)
  return stub
}

afterEach(() => {
  vi.unstubAllGlobals()
})

describe('PairForm', () => {
  it('waits for a device when there are no requests', () => {
    const wrapper = mountNova(PairForm, { props: { requests: [], loaded: true } })
    expect(wrapper.text()).toContain('Waiting for a device…')
    expect(wrapper.find('form').exists()).toBe(false)
    wrapper.unmount()
  })

  it('picks the only request, suggests its name and validates the PIN', async () => {
    const stub = stubPin({ status: 200, body: { status: true } })
    const wrapper = mountNova(PairForm, { props: { requests: [REQUEST], loaded: true } })
    await flushPromises()
    expect(wrapper.find('select').exists()).toBe(false)
    expect(wrapper.text()).toContain('A device wants to pair')
    expect(wrapper.find('[data-testid="pair-request"]').text()).toContain('Pixel 9 Pro')
    expect(wrapper.find('input[autocomplete="off"]').element.value).toBe('Pixel 9 Pro')

    await wrapper.find('input[inputmode="numeric"]').setValue('12')
    await wrapper.find('form').trigger('submit')
    expect(wrapper.text()).toContain('Enter the 4-digit PIN')
    expect(stub).not.toHaveBeenCalled()

    await wrapper.find('input[inputmode="numeric"]').setValue('1234')
    await wrapper.find('form').trigger('submit')
    await flushPromises()
    const [, init] = stub.mock.calls[0]
    expect(init.method).toBe('POST')
    expect(JSON.parse(init.body)).toEqual({ pairing_id: REQUEST.id, pin: '1234', name: 'Pixel 9 Pro' })
    expect(wrapper.text()).toContain('Pixel 9 Pro is paired')
    expect(wrapper.emitted('paired')[0]).toEqual(['Pixel 9 Pro'])
    wrapper.unmount()
  })

  it('names the device when pairing fails', async () => {
    stubPin({ status: 200, body: { status: false } })
    const wrapper = mountNova(PairForm, { props: { requests: [REQUEST], loaded: true } })
    await flushPromises()
    await wrapper.find('input[inputmode="numeric"]').setValue('9999')
    await wrapper.find('form').trigger('submit')
    await flushPromises()
    expect(wrapper.text()).toContain('Couldn’t pair Pixel 9 Pro')
    wrapper.unmount()
  })

  it('pre-fills the host\'s suggested name and shows the device type', async () => {
    const stub = stubPin({ status: 200, body: { status: true } })
    const request = { ...REQUEST, name: 'Galaxy S25 Ultra', app: 'Nebula', form: 'phone', suggested_name: 'Nebula from Ayush\'s Galaxy S25 Ultra' }
    const wrapper = mountNova(PairForm, { props: { requests: [request], loaded: true } })
    await flushPromises()
    const card = wrapper.find('[data-testid="pair-request"]')
    expect(card.text()).toContain('A device wants to pair')
    expect(card.text()).toContain('Nebula from Ayush\'s Galaxy S25 Ultra')
    expect(card.find('[data-form="phone"]').attributes('aria-label')).toBe('Phone')
    const field = wrapper.find('input[autocomplete="off"]')
    expect(field.element.value).toBe('Nebula from Ayush\'s Galaxy S25 Ultra')

    // The name stays editable before confirming.
    await field.setValue('  Couch phone ')
    await wrapper.find('input[inputmode="numeric"]').setValue('4321')
    await wrapper.find('form').trigger('submit')
    await flushPromises()
    expect(JSON.parse(stub.mock.calls[0][1].body)).toEqual({ pairing_id: REQUEST.id, pin: '4321', name: 'Couch phone' })
    expect(wrapper.emitted('paired')[0]).toEqual(['Couch phone'])
    wrapper.unmount()
  })

  it('uses the suggestion when the name is cleared and shows tablet and TV icons', async () => {
    const stub = stubPin({ status: 200, body: { status: true } })
    const tablet = { ...REQUEST, form: 'tablet', suggested_name: 'Nebula from Ayush\'s Tab S9' }
    const wrapper = mountNova(PairForm, { props: { requests: [tablet], loaded: true } })
    await flushPromises()
    expect(wrapper.find('[data-form="tablet"]').attributes('aria-label')).toBe('Tablet')
    await wrapper.find('input[autocomplete="off"]').setValue('')
    await wrapper.find('input[inputmode="numeric"]').setValue('1111')
    await wrapper.find('form').trigger('submit')
    await flushPromises()
    expect(JSON.parse(stub.mock.calls[0][1].body).name).toBe('Nebula from Ayush\'s Tab S9')

    await wrapper.setProps({ requests: [{ ...REQUEST, id: 'f'.repeat(32), form: 'tv', suggested_name: 'Nebula from Den TV' }] })
    await flushPromises()
    expect(wrapper.find('[data-form="tv"]').attributes('aria-label')).toBe('TV')
    expect(wrapper.find('input[autocomplete="off"]').element.value).toBe('Nebula from Den TV')
    wrapper.unmount()
  })

  it('keeps a name the user typed when requests refresh, and falls back for older hosts', async () => {
    const wrapper = mountNova(PairForm, { props: { requests: [{ ...REQUEST, suggested_name: 'Moonlight from Ayush\'s device' }], loaded: true } })
    await flushPromises()
    await wrapper.find('input[autocomplete="off"]').setValue('My laptop')
    await wrapper.setProps({ requests: [{ ...REQUEST, suggested_name: 'Moonlight from Ayush\'s device' }] })
    await flushPromises()
    expect(wrapper.find('input[autocomplete="off"]').element.value).toBe('My laptop')
    // No form factor: a generic icon without a label.
    expect(wrapper.find('[data-form="unknown"]').attributes('aria-hidden')).toBe('true')
    wrapper.unmount()

    const older = mountNova(PairForm, { props: { requests: [{ id: REQUEST.id, name: '', address: '10.0.0.2' }], loaded: true } })
    await flushPromises()
    expect(older.text()).toContain('Unknown device')
    expect(older.find('input[autocomplete="off"]').element.value).toBe('')
    older.unmount()
  })

  it('labels several requests with their suggested names', async () => {
    const requests = [
      { ...REQUEST, suggested_name: 'Nebula from Ayush\'s S25 Ultra' },
      { id: 'a'.repeat(32), name: 'roth', address: '10.0.0.3', suggested_name: 'Moonlight from Ayush\'s device' },
    ]
    const wrapper = mountNova(PairForm, { props: { requests, loaded: true } })
    await flushPromises()
    const labels = wrapper.findAll('option').map((o) => o.text())
    expect(labels).toContain('Nebula from Ayush\'s S25 Ultra — 192.168.1.20')
    expect(labels).toContain('Moonlight from Ayush\'s device — 10.0.0.3')
    wrapper.unmount()
  })

  it('renders the pre-filled form for review', async () => {
    const request = { ...REQUEST, address: '192.168.1.42', app: 'Nebula', form: 'phone', suggested_name: 'Nebula from Ayush\'s S25 Ultra' }
    const wrapper = mountNova(PairForm, { props: { requests: [request], loaded: true, hostName: 'atom' } })
    await flushPromises()
    const out = globalThis.process?.env?.PAIR_FORM_HTML
    if (out) {
      const { writeFileSync } = await import('node:fs')
      writeFileSync(out, wrapper.html())
    }
    expect(wrapper.text()).toContain('A device wants to pair')
    expect(wrapper.find('input[autocomplete="off"]').element.value).toBe('Nebula from Ayush\'s S25 Ultra')
    wrapper.unmount()
  })
})

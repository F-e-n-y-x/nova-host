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
    expect(wrapper.text()).toContain('Pairing request from Pixel 9 Pro')
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
})

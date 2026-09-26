import { afterEach, describe, expect, it, vi } from 'vitest'
import { nextTick } from 'vue'

vi.mock('../../../src_assets/common/assets/web/nova/api.js', () => ({
  getConfig: vi.fn(async () => ({ username: 'ayush' })),
  logout: vi.fn(),
}))

import AppShell from '../../../src_assets/common/assets/web/nova/AppShell.vue'
import { mountNova } from './helpers.js'

const mounted = []
afterEach(() => {
  mounted.splice(0).forEach((w) => w.unmount())
})

function tab(shift = false) {
  const event = new KeyboardEvent('keydown', { key: 'Tab', shiftKey: shift, bubbles: true, cancelable: true })
  document.dispatchEvent(event)
  return event
}

describe('AppShell drawer', () => {
  it('makes the page inert and locks scroll while open', async () => {
    const w = mountNova(AppShell, { slots: { default: '<button id="page-btn">Page</button>' } })
    mounted.push(w)
    await w.find('.nv-topbar__menu').trigger('click')
    await nextTick()

    expect(w.find('#nv-main').attributes('inert')).toBeDefined()
    expect(document.documentElement.classList.contains('nv-scroll-locked')).toBe(true)

    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))
    await nextTick()
    expect(w.find('#nv-main').attributes('inert')).toBeUndefined()
    expect(document.documentElement.classList.contains('nv-scroll-locked')).toBe(false)
    expect(document.activeElement).toBe(w.find('.nv-topbar__menu').element)
  })

  it('keeps Tab focus inside the drawer and its menu button', async () => {
    const w = mountNova(AppShell, { slots: { default: '<button id="page-btn">Page</button>' } })
    mounted.push(w)
    const menu = w.find('.nv-topbar__menu').element
    await w.find('.nv-topbar__menu').trigger('click')
    await nextTick()

    const focusables = [...w.find('#nv-sidebar').element.querySelectorAll('a[href], button:not([disabled])')]
    const last = focusables[focusables.length - 1]
    last.focus()
    expect(tab().defaultPrevented).toBe(true)
    expect(document.activeElement).toBe(menu)

    menu.focus()
    expect(tab(true).defaultPrevented).toBe(true)
    expect(document.activeElement).toBe(last)

    document.getElementById('page-btn').focus()
    tab()
    expect(document.activeElement).toBe(menu)
  })
})

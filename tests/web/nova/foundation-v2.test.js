import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { defineComponent, h, nextTick, ref } from 'vue'

vi.mock('../../../src_assets/common/assets/web/nova/api.js', () => ({
  fetchJson: vi.fn(),
  getConfig: vi.fn(async () => ({ username: 'ayush', sunshine_name: 'atom' })),
  logout: vi.fn(),
}))

import { fetchJson } from '../../../src_assets/common/assets/web/nova/api.js'
import NvActionMenu from '../../../src_assets/common/assets/web/nova/components/NvActionMenu.vue'
import NvConfirmDialog from '../../../src_assets/common/assets/web/nova/components/NvConfirmDialog.vue'
import NvSheet from '../../../src_assets/common/assets/web/nova/components/NvSheet.vue'
import NvTable from '../../../src_assets/common/assets/web/nova/components/NvTable.vue'
import NvFilterChips from '../../../src_assets/common/assets/web/nova/components/NvFilterChips.vue'
import NvSparkline from '../../../src_assets/common/assets/web/nova/components/NvSparkline.vue'
import NvStatBand from '../../../src_assets/common/assets/web/nova/components/NvStatBand.vue'
import NvAttention from '../../../src_assets/common/assets/web/nova/components/NvAttention.vue'
import NvArt from '../../../src_assets/common/assets/web/nova/components/NvArt.vue'
import NvTextField from '../../../src_assets/common/assets/web/nova/components/NvTextField.vue'
import NvSettingRow from '../../../src_assets/common/assets/web/nova/components/NvSettingRow.vue'
import NvLiveStrip from '../../../src_assets/common/assets/web/nova/components/NvLiveStrip.vue'
import NvCommandPalette from '../../../src_assets/common/assets/web/nova/components/NvCommandPalette.vue'
import NvPage from '../../../src_assets/common/assets/web/nova/components/NvPage.vue'
import AppShell from '../../../src_assets/common/assets/web/nova/AppShell.vue'
import { bitrateSeries, liveState, refreshLive, sessionSummary } from '../../../src_assets/common/assets/web/nova/live.js'
import { closePalette, matches, palette, registerSearchProvider, runSearch } from '../../../src_assets/common/assets/web/nova/search.js'
import { mountNova } from './helpers.js'

const mounted = []
const track = (w) => {
  mounted.push(w)
  return w
}
afterEach(() => {
  mounted.splice(0).forEach((w) => w.unmount())
  document.body.innerHTML = ''
  document.documentElement.classList.remove('nv-scroll-locked')
  closePalette()
})
beforeEach(() => {
  fetchJson.mockReset()
  fetchJson.mockImplementation(async () => ({}))
})

const session = {
  id: 's1', client_name: 'Pixel 9 Pro', app_name: 'Portal 2', fps_actual: 59.8, bitrate_kbps: 30400,
  latency_ms: { capture: 1.2, encode: 3.8, send: 1, total: 6 }, loss_pct: 0.1,
  samples: [{ bitrate_kbps: 29000 }, { bitrate_kbps: 31000 }, { bitrate_kbps: 30400 }],
}

describe('NvConfirmDialog', () => {
  it('focuses Cancel first and requires the checkbox before confirming', async () => {
    const onConfirm = vi.fn()
    const w = track(mountNova(NvConfirmDialog, {
      props: { open: true, title: 'Unpair all devices?', description: 'Every device must pair again.', confirmLabel: 'Unpair all',
        requireCheck: 'I understand', onConfirm },
    }))
    await nextTick()
    await nextTick()
    expect(document.activeElement.textContent.trim()).toBe('Cancel')
    const confirm = [...document.querySelectorAll('.nv-dialog button')].find((b) => b.textContent.trim() === 'Unpair all')
    expect(confirm.disabled).toBe(true)
    const box = document.querySelector('.nv-dialog input[type="checkbox"]')
    box.click()
    await nextTick()
    expect(confirm.disabled).toBe(false)
    confirm.click()
    expect(onConfirm).toHaveBeenCalledOnce()
    expect(w.props('open')).toBe(true)
  })

  it('requires the exact text when requireText is set', async () => {
    track(mountNova(NvConfirmDialog, {
      props: { open: true, title: 'Delete Portal?', description: 'Its artwork is removed too.', confirmLabel: 'Delete', requireText: 'Portal' },
    }))
    await nextTick()
    const confirm = [...document.querySelectorAll('.nv-dialog button')].find((b) => b.textContent.trim() === 'Delete')
    const field = document.querySelector('.nv-dialog input[type="text"]')
    field.value = 'portal'
    field.dispatchEvent(new Event('input'))
    await nextTick()
    expect(confirm.disabled).toBe(true)
    field.value = 'Portal'
    field.dispatchEvent(new Event('input'))
    await nextTick()
    expect(confirm.disabled).toBe(false)
  })
})

describe('NvActionMenu', () => {
  it('opens with the first item focused, moves with arrows, and returns focus on Escape', async () => {
    const onSelect = vi.fn()
    const w = track(mountNova(NvActionMenu, {
      props: { label: 'More actions', items: [{ id: 'a', label: 'Edit' }, { divider: true, id: 'x' }, { id: 'b', label: 'Delete', danger: true, onSelect }] },
    }))
    await w.get('.nv-menu__button').trigger('click')
    await nextTick()
    const items = [...document.querySelectorAll('[role="menuitem"]')]
    expect(items).toHaveLength(2)
    expect(document.activeElement).toBe(items[0])
    document.querySelector('[role="menu"]').dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowDown', bubbles: true }))
    expect(document.activeElement).toBe(items[1])
    items[1].click()
    expect(onSelect).toHaveBeenCalledOnce()
    expect(w.emitted('select')[0]).toEqual(['b'])
    await nextTick()
    expect(document.querySelector('[role="menu"]')).toBeNull()
    expect(document.activeElement).toBe(w.get('.nv-menu__button').element)
  })

  it('renders checked items as menuitemradio', async () => {
    const w = track(mountNova(NvActionMenu, {
      props: { label: 'Appearance', items: [{ id: 'light', label: 'Light', checked: true }, { id: 'dark', label: 'Dark', checked: false }] },
    }))
    await w.get('.nv-menu__button').trigger('click')
    await nextTick()
    const radios = [...document.querySelectorAll('[role="menuitemradio"]')]
    expect(radios.map((r) => r.getAttribute('aria-checked'))).toEqual(['true', 'false'])
  })
})

describe('NvSheet', () => {
  it('is a labelled modal, closes on Escape and can veto closing', async () => {
    const veto = ref(true)
    const Host = defineComponent({
      setup() {
        const open = ref(true)
        return () => h(NvSheet, { open: open.value, 'onUpdate:open': (v) => { open.value = v }, title: 'Portal 2', beforeClose: () => !veto.value },
          { default: () => h('p', 'Body') })
      },
    })
    track(mountNova(Host))
    await nextTick()
    const panel = document.querySelector('.nv-sheet__panel')
    expect(panel.getAttribute('role')).toBe('dialog')
    expect(panel.getAttribute('aria-modal')).toBe('true')
    expect(document.documentElement.classList.contains('nv-scroll-locked')).toBe(true)
    document.querySelector('.nv-sheet').dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))
    await nextTick()
    expect(document.querySelector('.nv-sheet')).not.toBeNull()
    veto.value = false
    document.querySelector('.nv-sheet').dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))
    await nextTick()
    await nextTick()
    expect(document.querySelector('.nv-sheet')).toBeNull()
    expect(document.documentElement.classList.contains('nv-scroll-locked')).toBe(false)
  })
})

describe('NvTable', () => {
  it('uses table semantics and toggles aria-sort on sortable headers', async () => {
    const w = track(mountNova(NvTable, {
      props: {
        columns: [{ key: 'name', label: 'Name', sortable: true }, { key: 'type', label: 'Type' }],
        rows: [{ id: 1, name: 'Portal', type: 'Game' }],
        sort: { key: 'name', dir: 'asc' },
      },
    }))
    const th = w.get('th')
    expect(th.attributes('aria-sort')).toBe('ascending')
    await th.get('button').trigger('click')
    expect(w.emitted('update:sort')[0]).toEqual([{ key: 'name', dir: 'desc' }])
    expect(w.findAll('th')[1].attributes('aria-sort')).toBeUndefined()
    expect(w.get('td').text()).toBe('Portal')
  })

  it('shows skeleton rows and a hidden loading status while loading', () => {
    const w = track(mountNova(NvTable, { props: { columns: [{ key: 'a', label: 'A' }], loading: true, skeletonRows: 3 } }))
    expect(w.findAll('.nv-table__skeleton')).toHaveLength(3)
    expect(w.get('[role="status"]').text()).toBe('Loading…')
  })
})

describe('small components', () => {
  it('NvFilterChips toggles single and multiple selections with aria-pressed', async () => {
    const w = track(mountNova(NvFilterChips, { props: { label: 'Levels', multiple: true, modelValue: ['error'],
      options: [{ value: 'error', label: 'Errors', count: 2 }, { value: 'warn', label: 'Warnings', count: 5 }] } }))
    const chips = w.findAll('.nv-chip')
    expect(chips[0].attributes('aria-pressed')).toBe('true')
    await chips[1].trigger('click')
    expect(w.emitted('update:modelValue')[0]).toEqual([['error', 'warn']])
  })

  it('NvSparkline is an image with a label and draws a path from values', () => {
    const w = track(mountNova(NvSparkline, { props: { values: [1, 3, 2], label: 'Network, now 2 Mbps' } }))
    expect(w.get('svg').attributes('role')).toBe('img')
    expect(w.get('svg').attributes('aria-label')).toBe('Network, now 2 Mbps')
    expect(w.get('.nv-spark__line').attributes('d')).toMatch(/^M0\.0 /)
  })

  it('NvStatBand renders one cell per entry with its status dot', () => {
    const w = track(mountNova(NvStatBand, { props: { cells: [{ key: 's', label: 'Stream', value: 'Ready', status: 'success' }, { key: 'e', label: 'Encoder', value: 'NVENC', sub: 'H.264 · HEVC' }] } }))
    expect(w.findAll('.nv-band__cell')).toHaveLength(2)
    expect(w.find('.nv-band__dot--success').exists()).toBe(true)
    expect(w.text()).toContain('H.264 · HEVC')
  })

  it('NvAttention shows the first issue and expands the rest', async () => {
    const w = track(mountNova(NvAttention, { props: { issues: [
      { id: 'a', title: 'Clipboard sync can’t run', detail: 'xclip isn’t installed.', command: 'sudo apt install xclip' },
      { id: 'b', title: 'Update available', detail: '0.2.0 is out.' },
    ] } }))
    expect(w.findAll('.nv-attention__row')).toHaveLength(1)
    expect(w.get('.nv-attention__more').text()).toContain('2 things need attention')
    await w.get('.nv-attention__more').trigger('click')
    expect(w.findAll('.nv-attention__row')).toHaveLength(2)
  })

  it('NvArt falls back to a labelled placeholder when there is no image', () => {
    const w = track(mountNova(NvArt, { props: { title: 'Far Cry 5' } }))
    expect(w.get('[role="img"]').attributes('aria-label')).toBe('Far Cry 5')
    expect(w.text()).toContain('Far Cry 5')
  })

  it('NvTextField forwards attributes to the input and toggles password visibility', async () => {
    const w = track(mountNova(NvTextField, { props: { label: 'Password', type: 'password' }, attrs: { inputmode: 'text', maxlength: '64' } }))
    const input = w.get('input')
    expect(input.attributes('maxlength')).toBe('64')
    expect(input.attributes('type')).toBe('password')
    await w.get('.nv-field__reveal').trigger('click')
    expect(w.get('input').attributes('type')).toBe('text')
    expect(w.get('.nv-field__reveal').attributes('aria-pressed')).toBe('true')
  })

  it('NvSettingRow shows an inline error and a More disclosure', async () => {
    const w = track(mountNova(NvSettingRow, { props: { label: 'Port', description: 'Base port.', more: 'Long help', error: 'Use 1024–65535.' } }))
    expect(w.text()).toContain('Use 1024–65535.')
    const toggle = w.get('.nv-setting__more-toggle')
    expect(toggle.attributes('aria-expanded')).toBe('false')
    await toggle.trigger('click')
    expect(toggle.attributes('aria-expanded')).toBe('true')
    expect(w.get('.nv-setting__more').isVisible()).toBe(true)
  })

  it('NvLiveStrip summarises the session and links to Overview', () => {
    const w = track(mountNova(NvLiveStrip, { props: { session } }))
    expect(w.text()).toContain('Portal 2 on Pixel 9 Pro')
    expect(w.text()).toContain('60 fps · 30 Mbps · 6.0 ms')
    expect(w.get('svg').attributes('aria-label')).toContain('30 Mbps')
    expect(w.get('a').attributes('href')).toBe('/')
  })
})

describe('live store', () => {
  it('reports the sessions API as unavailable on 404 without an error', async () => {
    fetchJson.mockRejectedValueOnce(Object.assign(new Error('404'), { status: 404 }))
    await refreshLive()
    expect(liveState.available).toBe(false)
    expect(liveState.error).toBeNull()
    expect(liveState.sessions).toEqual([])
  })

  it('stores sessions and formats their headline numbers', async () => {
    fetchJson.mockResolvedValueOnce({ sessions: [session] })
    await refreshLive()
    expect(liveState.available).toBe(true)
    expect(sessionSummary(liveState.sessions[0])).toBe('60 fps · 30 Mbps · 6.0 ms')
    expect(bitrateSeries(session)).toEqual([29, 31, 30.4])
  })
})

describe('search', () => {
  it('matches every word in any field', () => {
    expect(matches('far cry', 'Far Cry 5')).toBe(true)
    expect(matches('far 6', 'Far Cry 5')).toBe(false)
  })

  it('runs providers in order and drops empty groups', async () => {
    const remove = registerSearchProvider({ id: 'test', order: -1, group: 'Test', search: (q) => (q === 'x' ? [{ id: 't', label: 'X' }] : []) })
    const t = (k) => k
    const groups = await runSearch('x', { t, te: () => false })
    expect(groups[0].id).toBe('test')
    remove()
    expect((await runSearch('x', { t, te: () => false })).some((g) => g.id === 'test')).toBe(false)
  })

  it('the palette navigates to the chosen page with Enter', async () => {
    const w = track(mountNova(NvCommandPalette))
    const router = w.vm.$router
    palette.query = 'settings'
    palette.open = true
    await new Promise((r) => setTimeout(r, 200))
    await nextTick()
    const input = document.querySelector('.nv-cmd__input')
    expect(input.getAttribute('role')).toBe('combobox')
    expect(document.querySelector('[role="option"][aria-selected="true"]').textContent).toContain('Settings')
    input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))
    await new Promise((r) => setTimeout(r, 0))
    expect(router.currentRoute.value.path).toBe('/settings')
    expect(palette.open).toBe(false)
  })
})

describe('shell + page', () => {
  it('puts the page title in the top bar and opens the palette with Ctrl+K', async () => {
    const Page = defineComponent({ render: () => h(NvPage, { title: 'Library' }, { actions: () => h('button', { id: 'primary' }, 'Add games') }) })
    const w = track(mountNova(AppShell, { slots: { default: () => h(Page) } }))
    await new Promise((r) => setTimeout(r, 0))
    await nextTick()
    expect(w.get('.nv-topbar__title').text()).toBe('Library')
    expect(document.querySelectorAll('h1')).toHaveLength(1)
    expect(w.get('#nv-topbar-actions').find('#primary').exists()).toBe(true)
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'k', ctrlKey: true, bubbles: true }))
    expect(palette.open).toBe(true)
  })

  it('shows the host name and groups the navigation', async () => {
    const w = track(mountNova(AppShell))
    await new Promise((r) => setTimeout(r, 0))
    await nextTick()
    expect(w.get('.nv-host__name').text()).toBe('atom')
    expect(w.findAll('.nv-sidebar__group').map((g) => g.text())).toEqual(['Host', 'System'])
    expect(w.findAll('.nv-nav-link').map((l) => l.text())).toEqual(
      expect.arrayContaining(['Overview', 'Library', 'Devices', 'Pair a device', 'Settings', 'Logs & diagnostics']))
  })
})

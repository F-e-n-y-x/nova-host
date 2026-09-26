import { afterEach, describe, expect, it } from 'vitest'
import { nextTick } from 'vue'

import { mountNova } from './helpers.js'
import NvAlert from '../../../src_assets/common/assets/web/nova/components/NvAlert.vue'
import NvBadge from '../../../src_assets/common/assets/web/nova/components/NvBadge.vue'
import NvButton from '../../../src_assets/common/assets/web/nova/components/NvButton.vue'
import NvCard from '../../../src_assets/common/assets/web/nova/components/NvCard.vue'
import NvDataList from '../../../src_assets/common/assets/web/nova/components/NvDataList.vue'
import NvDialog from '../../../src_assets/common/assets/web/nova/components/NvDialog.vue'
import NvEmptyState from '../../../src_assets/common/assets/web/nova/components/NvEmptyState.vue'
import NvIconButton from '../../../src_assets/common/assets/web/nova/components/NvIconButton.vue'
import NvNumberField from '../../../src_assets/common/assets/web/nova/components/NvNumberField.vue'
import NvSegmentedControl from '../../../src_assets/common/assets/web/nova/components/NvSegmentedControl.vue'
import NvSelect from '../../../src_assets/common/assets/web/nova/components/NvSelect.vue'
import NvSettingRow from '../../../src_assets/common/assets/web/nova/components/NvSettingRow.vue'
import NvSkeleton from '../../../src_assets/common/assets/web/nova/components/NvSkeleton.vue'
import NvStatusDot from '../../../src_assets/common/assets/web/nova/components/NvStatusDot.vue'
import NvSwitch from '../../../src_assets/common/assets/web/nova/components/NvSwitch.vue'
import NvTextField from '../../../src_assets/common/assets/web/nova/components/NvTextField.vue'
import NvToastHost from '../../../src_assets/common/assets/web/nova/components/NvToastHost.vue'
import { dismissToast, showToast, toasts } from '../../../src_assets/common/assets/web/nova/toast.js'

const wrappers = []
function track(wrapper) {
  wrappers.push(wrapper)
  return wrapper
}

afterEach(() => {
  while (wrappers.length) wrappers.pop().unmount()
  document.body.innerHTML = ''
})

describe('NvButton', () => {
  it('renders a native button with variant classes', () => {
    const w = track(mountNova(NvButton, { props: { variant: 'primary' }, slots: { default: 'Save' } }))
    const button = w.get('button')
    expect(button.attributes('type')).toBe('button')
    expect(button.classes()).toContain('nv-btn--primary')
    expect(button.text()).toBe('Save')
  })

  it('marks loading buttons busy and disabled', () => {
    const w = track(mountNova(NvButton, { props: { loading: true }, slots: { default: 'Save' } }))
    const button = w.get('button')
    expect(button.attributes('aria-busy')).toBe('true')
    expect(button.attributes('disabled')).toBeDefined()
  })

  it('renders links for href and to', () => {
    const link = track(mountNova(NvButton, { props: { href: 'https://example.com' }, slots: { default: 'Docs' } }))
    expect(link.get('a').attributes('href')).toBe('https://example.com')
    const route = track(mountNova(NvButton, { props: { to: '/pair' }, slots: { default: 'Pair' } }))
    expect(route.get('a').attributes('href')).toBe('/pair')
  })
})

describe('NvIconButton', () => {
  it('uses the label as accessible name and tooltip', () => {
    const w = track(mountNova(NvIconButton, { props: { label: 'Close' }, slots: { default: '<svg />' } }))
    expect(w.get('button').attributes('aria-label')).toBe('Close')
    expect(w.get('button').attributes('title')).toBe('Close')
  })
})

describe('NvCard', () => {
  it('labels the section with its heading', () => {
    const w = track(mountNova(NvCard, { props: { title: 'Health' }, slots: { default: '<p>body</p>' } }))
    const section = w.get('section')
    const heading = w.get('h2')
    expect(section.attributes('aria-labelledby')).toBe(heading.attributes('id'))
    expect(heading.text()).toBe('Health')
  })
})

describe('NvSwitch', () => {
  it('exposes switch semantics and toggles', async () => {
    const w = track(mountNova(NvSwitch, { props: { modelValue: false, label: 'Clipboard sync', 'onUpdate:modelValue': (v) => w.setProps({ modelValue: v }) } }))
    const button = w.get('[role="switch"]')
    expect(button.attributes('aria-checked')).toBe('false')
    expect(button.attributes('aria-label')).toBe('Clipboard sync')
    await button.trigger('click')
    expect(w.emitted('update:modelValue')[0]).toEqual([true])
    expect(w.get('[role="switch"]').attributes('aria-checked')).toBe('true')
  })

  it('prefers aria-labelledby when given', () => {
    const w = track(mountNova(NvSwitch, { props: { labelledby: 'row-label' } }))
    const button = w.get('[role="switch"]')
    expect(button.attributes('aria-labelledby')).toBe('row-label')
    expect(button.attributes('aria-label')).toBeUndefined()
  })
})

describe('NvSegmentedControl', () => {
  const options = ['Auto', 'Vblank', 'Timer']

  it('marks the selected option pressed and uses a roving tabindex', () => {
    const w = track(mountNova(NvSegmentedControl, { props: { modelValue: 'Vblank', options, label: 'Capture pacing' } }))
    expect(w.get('[role="group"]').attributes('aria-label')).toBe('Capture pacing')
    const buttons = w.findAll('button')
    expect(buttons.map((b) => b.attributes('aria-pressed'))).toEqual(['false', 'true', 'false'])
    expect(buttons.map((b) => b.attributes('tabindex'))).toEqual(['-1', '0', '-1'])
  })

  it('moves the selection with arrow keys, wrapping around', async () => {
    const w = track(mountNova(NvSegmentedControl, { props: { modelValue: 'Timer', options, label: 'Pacing' } }))
    await w.findAll('button')[2].trigger('keydown', { key: 'ArrowRight' })
    expect(w.emitted('update:modelValue').at(-1)).toEqual(['Auto'])
    await w.findAll('button')[2].trigger('keydown', { key: 'Home' })
    expect(w.emitted('update:modelValue').at(-1)).toEqual(['Auto'])
    await w.findAll('button')[0].trigger('keydown', { key: 'ArrowLeft' })
    expect(w.emitted('update:modelValue').at(-1)).toEqual(['Timer'])
  })
})

describe('NvSettingRow', () => {
  it('shows the changed marker and emits reset', async () => {
    const w = track(mountNova(NvSettingRow, {
      props: { label: 'Capture pacing', description: 'How frames are timed.', modified: true },
      slots: { default: '<template #default="{ labelId, descriptionId }"><span class="probe" :data-label="labelId" :data-desc="descriptionId"></span></template>' },
    }))
    expect(w.text()).toContain('Changed')
    const reset = w.get('.nv-setting__reset')
    expect(reset.text()).toContain('Capture pacing')
    await reset.trigger('click')
    expect(w.emitted('reset')).toHaveLength(1)
    const probe = w.get('.probe')
    expect(document.getElementById(probe.attributes('data-label')).textContent).toBe('Capture pacing')
    expect(document.getElementById(probe.attributes('data-desc')).textContent).toBe('How frames are timed.')
  })

  it('hides the marker and reset when unchanged, and shows warnings', () => {
    const w = track(mountNova(NvSettingRow, { props: { label: 'Clipboard sync', warning: 'xclip is missing' } }))
    expect(w.find('.nv-setting__reset').exists()).toBe(false)
    expect(w.text()).not.toContain('Changed')
    expect(w.get('[role="note"]').text()).toContain('xclip is missing')
  })
})

describe('form fields', () => {
  it('NvTextField links label, hint and error', () => {
    const w = track(mountNova(NvTextField, { props: { label: 'Host name', hint: 'Shown to clients', error: 'Required' } }))
    const input = w.get('input')
    expect(w.get('label').attributes('for')).toBe(input.attributes('id'))
    const described = input.attributes('aria-describedby').split(' ')
    expect(described).toHaveLength(2)
    expect(input.attributes('aria-invalid')).toBe('true')
  })

  it('NvNumberField emits numbers and null for empty', async () => {
    const w = track(mountNova(NvNumberField, { props: { label: 'Maximum frame rate', unit: 'fps', modelValue: 0 } }))
    const input = w.get('input')
    await input.setValue('60')
    expect(w.emitted('update:modelValue').at(-1)).toEqual([60])
    await input.setValue('')
    expect(w.emitted('update:modelValue').at(-1)).toEqual([null])
    expect(input.attributes('aria-describedby')).toContain('-unit')
  })

  it('NvSelect renders string and object options', async () => {
    const w = track(mountNova(NvSelect, { props: { label: 'Capture', options: ['Auto', { value: 'kms', label: 'KMS' }], modelValue: 'Auto' } }))
    expect(w.findAll('option').map((o) => o.text())).toEqual(['Auto', 'KMS'])
    await w.get('select').setValue('kms')
    expect(w.emitted('update:modelValue').at(-1)).toEqual(['kms'])
  })
})

describe('NvAlert', () => {
  it('announces live danger alerts', () => {
    const w = track(mountNova(NvAlert, { props: { variant: 'danger', title: 'Failed', live: true } }))
    expect(w.get('.nv-alert').attributes('role')).toBe('alert')
  })

  it('is silent unless live, and dismissible', async () => {
    const w = track(mountNova(NvAlert, { props: { variant: 'info', title: 'Note', dismissible: true } }))
    expect(w.get('.nv-alert').attributes('role')).toBeUndefined()
    await w.get('button[aria-label="Dismiss"]').trigger('click')
    expect(w.emitted('dismiss')).toHaveLength(1)
  })
})

describe('small components', () => {
  it('NvStatusDot always carries a text label', () => {
    const w = track(mountNova(NvStatusDot, { props: { status: 'success', label: 'Online', hideLabel: true } }))
    expect(w.text()).toBe('Online')
    expect(w.get('.nv-visually-hidden').text()).toBe('Online')
  })

  it('NvSkeleton is hidden from assistive tech', () => {
    const w = track(mountNova(NvSkeleton, { props: { lines: 3 } }))
    expect(w.get('.nv-skeleton-group').attributes('aria-hidden')).toBe('true')
    expect(w.findAll('.nv-skeleton')).toHaveLength(3)
  })

  it('NvDataList renders a definition list', () => {
    const w = track(mountNova(NvDataList, { props: { items: [{ term: 'GPU', value: 'GTX 1080 Ti' }, { term: 'Driver', value: '580', mono: true }] } }))
    expect(w.findAll('dt').map((d) => d.text())).toEqual(['GPU', 'Driver'])
    expect(w.findAll('dd')[1].classes()).toContain('nv-mono')
  })

  it('NvBadge and NvEmptyState render content', () => {
    expect(track(mountNova(NvBadge, { props: { variant: 'success' }, slots: { default: 'Online' } })).classes()).toContain('nv-badge--success')
    const empty = track(mountNova(NvEmptyState, { props: { title: 'No devices', description: 'Pair one' } }))
    expect(empty.text()).toContain('No devices')
  })
})

describe('NvDialog', () => {
  const Host = {
    components: { NvDialog },
    data: () => ({ open: false }),
    template: `<div>
      <button id="opener" @click="open = true">Open</button>
      <NvDialog v-model:open="open" title="Restart the host?" description="Streams disconnect.">
        <button id="first">Cancel</button>
        <template #footer><button id="last">Restart</button></template>
      </NvDialog>
    </div>`,
  }

  it('focuses inside when opened, closes on Escape and restores focus', async () => {
    const w = track(mountNova(Host))
    const opener = document.getElementById('opener')
    opener.focus()
    await w.get('#opener').trigger('click')
    await nextTick()
    await nextTick()
    const dialog = document.querySelector('[role="dialog"]')
    expect(dialog.getAttribute('aria-modal')).toBe('true')
    expect(document.getElementById(dialog.getAttribute('aria-labelledby')).textContent).toBe('Restart the host?')
    expect(dialog.contains(document.activeElement)).toBe(true)

    dialog.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }))
    await nextTick()
    await nextTick()
    expect(document.querySelector('[role="dialog"]')).toBeNull()
    expect(document.activeElement).toBe(opener)
  })

  it('traps Tab focus inside the dialog', async () => {
    const w = track(mountNova(Host))
    await w.get('#opener').trigger('click')
    await nextTick()
    await nextTick()
    const last = document.getElementById('last')
    last.focus()
    last.dispatchEvent(new KeyboardEvent('keydown', { key: 'Tab', bubbles: true, cancelable: true }))
    const closeButton = document.querySelector('.nv-dialog__close')
    expect(document.activeElement).toBe(closeButton)
    closeButton.dispatchEvent(new KeyboardEvent('keydown', { key: 'Tab', shiftKey: true, bubbles: true, cancelable: true }))
    expect(document.activeElement).toBe(last)
  })
})

describe('toasts', () => {
  it('renders queued toasts in a polite live region and dismisses them', async () => {
    const w = track(mountNova(NvToastHost))
    const id = showToast('Saved', { variant: 'success', timeout: 0 })
    await nextTick()
    expect(w.get('.nv-toasts').attributes('aria-live')).toBe('polite')
    expect(w.text()).toContain('Saved')
    dismissToast(id)
    await nextTick()
    expect(toasts).toHaveLength(0)
  })
})

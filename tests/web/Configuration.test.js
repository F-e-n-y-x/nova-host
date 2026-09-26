import { flushPromises } from '@vue/test-utils'
import { reactive } from 'vue'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import Config from '../../src_assets/common/assets/web/Config.vue'
import KeybindingsEditor from '../../src_assets/common/assets/web/configs/components/KeybindingsEditor.vue'
import PrepCommandsEditor from '../../src_assets/common/assets/web/configs/components/PrepCommandsEditor.vue'
import SettingField from '../../src_assets/common/assets/web/configs/components/SettingField.vue'
import {
  DEFAULTS, boolRepresentation, buildSavePayload, changedKeys, isModified, matchesSearch, optionApplies,
  prepareConfig, primaryEncoderGroups, splitDescription, validateOption,
} from '../../src_assets/common/assets/web/configs/settings_model.js'
import { ENCODER_GROUPS, GROUPS, OPTIONS, SECTIONS } from '../../src_assets/common/assets/web/configs/settings_schema.js'
import en from '../../src_assets/common/assets/web/public/assets/locale/en.json'
import { mountNova } from './nova/helpers.js'

const lookup = (key) => key.split('.').reduce((node, part) => (node ? node[part] : undefined), en)

describe('settings model', () => {
  it('treats the config file spelling of a default as unchanged', () => {
    expect(isModified('port', '47989')).toBe(false)
    expect(isModified('port', 47990)).toBe(true)
    expect(isModified('upnp', 'disabled')).toBe(false)
    expect(isModified('upnp', 'false')).toBe(false)
    expect(isModified('upnp', 'enabled')).toBe(true)
    expect(isModified('capture', '')).toBe(false)
    expect(isModified('sunshine_name', 'atom')).toBe(true)
    expect(isModified('global_prep_cmd', [])).toBe(false)
  })

  it('keeps the on/off spelling an option already uses', () => {
    expect(boolRepresentation('enabled', 'enabled')).toEqual(['enabled', 'disabled'])
    expect(boolRepresentation('true', 'disabled')).toEqual(['true', 'false'])
    expect(boolRepresentation(undefined, 'disabled')).toEqual(['enabled', 'disabled'])
  })

  it('prepares the API response: strips non-settings, parses JSON options, fills defaults', () => {
    const { config, platform } = prepareConfig({
      status: true, platform: 'linux', version: '0.1.0', username: 'ayush',
      sunshine_name: 'atom', global_prep_cmd: '[{"do":"a","undo":"b"}]',
    })
    expect(platform).toBe('linux')
    expect(config).not.toHaveProperty('username')
    expect(config).not.toHaveProperty('version')
    expect(config.global_prep_cmd).toEqual([{ do: 'a', undo: 'b' }])
    expect(config.port).toBe(DEFAULTS.port)
  })

  it('saves every non-default setting (the host rewrites the whole file) and nothing else', () => {
    const { config } = prepareConfig({ platform: 'linux', username: 'ayush', sunshine_name: 'atom', origin_web_ui_allowed: 'wan' })
    config.max_bitrate = 20000
    const payload = buildSavePayload(config)
    expect(payload).toEqual({ sunshine_name: 'atom', origin_web_ui_allowed: 'wan', max_bitrate: 20000 })
  })

  it('saves nested list settings from reactive state', () => {
    const { config } = prepareConfig({ platform: 'linux' })
    const state = reactive(config)
    state.global_prep_cmd.push({ do: 'start', undo: 'stop' })
    expect(buildSavePayload(state)).toEqual({ global_prep_cmd: [{ do: 'start', undo: 'stop' }] })
  })

  it('lists the keys edited since the last save', () => {
    expect(changedKeys({ port: '47989', upnp: 'enabled' }, { port: 47989, upnp: 'disabled' })).toEqual(['upnp'])
  })

  it('shows options only where they apply', () => {
    expect(optionApplies('capture_pacing', { ...DEFAULTS }, 'linux')).toBe(true)
    expect(optionApplies('capture_pacing', { ...DEFAULTS }, 'windows')).toBe(false)
    expect(optionApplies('gamepad', { ...DEFAULTS, controller: 'disabled' }, 'linux')).toBe(false)
    expect(optionApplies('dd_manual_resolution', { ...DEFAULTS, dd_resolution_option: 'manual' }, 'windows')).toBe(true)
  })

  it('puts the encoder in use first', () => {
    expect([...primaryEncoderGroups({ encoder: '' }, ['nvenc', 'software'], 'linux')]).toEqual(['nv', 'sw'])
    expect([...primaryEncoderGroups({ encoder: 'vaapi' }, ['nvenc'], 'linux')]).toEqual(['vaapi'])
  })

  it('matches every search word against key, label and description', () => {
    expect(matchesSearch('bit rate', 'max_bitrate', ['Maximum Bitrate', 'The maximum rate'])).toBe(true)
    expect(matchesSearch('max_bitrate', 'max_bitrate', [])).toBe(true)
    expect(matchesSearch('audio', 'max_bitrate', ['Maximum Bitrate'])).toBe(false)
  })

  it('validates numbers, ranges and formats', () => {
    expect(validateOption('port', 80)).toEqual({ key: 'nova.settings.errors.min', params: { min: 1029 } })
    expect(validateOption('max_fps_target', 60.5)).toEqual({ key: 'nova.settings.errors.integer' })
    expect(validateOption('port', 47989)).toBeNull()
    expect(validateOption('dd_manual_resolution', '2560x')).toEqual({ key: 'nova.settings.errors.resolution' })
  })

  it('has English text for every choice, group and section it names', () => {
    const missing = []
    for (const option of Object.values(OPTIONS)) {
      const lists = typeof option.choices === 'function'
        ? ['linux', 'windows', 'macos', 'freebsd'].flatMap((p) => option.choices(p, { ...DEFAULTS }))
        : option.choices || []
      for (const choice of lists) {
        for (const key of [choice.label, choice.suffix].filter(Boolean)) if (lookup(key) === undefined) missing.push(key)
      }
      const descKeys = option.descKeys && !Array.isArray(option.descKeys) ? Object.values(option.descKeys).flat() : option.descKeys || []
      for (const key of descKeys) if (lookup(key) === undefined) missing.push(key)
    }
    for (const key of Object.values(GROUPS)) if (lookup(key) === undefined) missing.push(key)
    for (const group of ENCODER_GROUPS) if (lookup(group.titleKey) === undefined) missing.push(group.titleKey)
    for (const section of SECTIONS) {
      for (const part of ['title', 'summary']) {
        const key = `nova.settings.sections.${section.id}.${part}`
        if (lookup(key) === undefined) missing.push(key)
      }
    }
    expect(missing).toEqual([])
  })

  it('splits long descriptions into a lead sentence and the rest', () => {
    const text = 'First sentence is here. ' + 'More detail follows. '.repeat(10)
    const { lead, rest } = splitDescription(text)
    expect(lead).toBe('First sentence is here.')
    expect(rest.length).toBeGreaterThan(0)
  })
})

describe('setting field', () => {
  it('toggles a switch using the option\'s own spelling and offers Reset once changed', async () => {
    const wrapper = mountNova(SettingField, {
      props: { optionKey: 'upnp', value: 'disabled', config: { ...DEFAULTS }, platform: 'linux' },
    })
    const toggle = wrapper.get('[role="switch"]')
    expect(toggle.attributes('aria-checked')).toBe('false')
    expect(toggle.attributes('aria-labelledby')).toBeTruthy()
    await toggle.trigger('click')
    expect(wrapper.emitted('update')[0]).toEqual(['upnp', 'enabled'])

    await wrapper.setProps({ value: 'enabled' })
    expect(wrapper.text()).toContain('Changed')
    await wrapper.get('.nv-cfg-row__reset').trigger('click')
    expect(wrapper.emitted('reset')[0]).toEqual(['upnp'])
    wrapper.unmount()
  })

  it('uses a segmented control for a few short choices', () => {
    const wrapper = mountNova(SettingField, {
      props: { optionKey: 'capture_pacing', value: 'auto', config: { ...DEFAULTS }, platform: 'linux' },
    })
    expect(wrapper.findAll('[aria-pressed]').length).toBe(3)
    wrapper.unmount()
  })

  it('shows inline errors', () => {
    const wrapper = mountNova(SettingField, {
      props: { optionKey: 'port', value: 80, config: { ...DEFAULTS, port: 80 }, platform: 'linux',
        error: { key: 'nova.settings.errors.min', params: { min: 1029 } } },
    })
    expect(wrapper.text()).toContain('Use 1029 or more.')
    expect(wrapper.get('input').attributes('aria-invalid')).toBe('true')
    wrapper.unmount()
  })
})

describe('list editors', () => {
  it('serializes only complete key mappings', async () => {
    const wrapper = mountNova(KeybindingsEditor, { props: { modelValue: '[0x10,0xA0]' } })
    expect(wrapper.findAll('.nv-keymap__row:not(.nv-keymap__row--head)').length).toBe(1)
    await wrapper.get('.nv-keymap__add').trigger('click')
    await flushPromises()
    // The new, empty row is not written back.
    const updates = wrapper.emitted('update:modelValue') || []
    expect(updates.every(([value]) => value === '[0x10,0xA0]')).toBe(true)
    wrapper.unmount()
  })

  it('labels every prep command field and removes rows', async () => {
    const wrapper = mountNova(PrepCommandsEditor, {
      props: { modelValue: [{ do: 'a', undo: 'b' }], platform: 'linux' },
    })
    for (const input of wrapper.findAll('input')) {
      expect(wrapper.find(`label[for="${input.attributes('id')}"]`).exists()).toBe(true)
    }
    await wrapper.get('button[aria-label="Remove command 1"]').trigger('click')
    expect(wrapper.emitted('update:modelValue')[0]).toEqual([[]])
    wrapper.unmount()
  })
})

describe('settings page', () => {
  let posted

  beforeEach(() => {
    posted = []
    vi.stubGlobal('IntersectionObserver', class { observe() {} disconnect() {} })
    vi.stubGlobal('fetch', vi.fn(async (url, options = {}) => {
      const path = String(url)
      if (path.includes('/api/config') && options.method === 'POST') {
        posted.push(JSON.parse(options.body))
        return new Response(JSON.stringify({ status: true }), { status: 200 })
      }
      if (path.includes('/api/config')) {
        return new Response(JSON.stringify({
          status: true, platform: 'linux', version: '0.1.0', username: 'ayush', sunshine_name: 'atom',
        }), { status: 200 })
      }
      if (path.includes('/api/logs')) {
        return new Response('[2026-09-27 00:00:00.000]: Info: Found H.264 encoder: h264_nvenc [nvenc]\n', { status: 200 })
      }
      return new Response('{}', { status: 200 })
    }))
  })

  afterEach(() => {
    vi.unstubAllGlobals()
  })

  it('edits, counts unsaved changes, and saves the full non-default config', async () => {
    const wrapper = mountNova(Config)
    await flushPromises()

    expect(wrapper.find('#display').exists()).toBe(true)
    expect(wrapper.find('#capture_pacing').exists()).toBe(true)
    // Windows-only options are not shown on Linux.
    expect(wrapper.find('#virtual_sink').exists()).toBe(false)
    // The detected NVENC group is expanded and marked.
    expect(wrapper.text()).toContain('In use')

    await wrapper.get('#upnp [role="switch"]').trigger('click')
    expect(wrapper.text()).toContain('1 unsaved change')

    const save = wrapper.findAll('.nv-savebar button').find((b) => b.text() === 'Save')
    await save.trigger('click')
    await flushPromises()
    expect(posted).toEqual([{ sunshine_name: 'atom', upnp: 'enabled' }])
    await vi.waitFor(() => expect(wrapper.text()).toContain('Restart now'))
    expect(wrapper.text()).not.toContain('unsaved change')
    wrapper.unmount()
  })

  it('filters by search and discards edits', async () => {
    const wrapper = mountNova(Config)
    await flushPromises()

    await wrapper.get('#upnp [role="switch"]').trigger('click')
    const discard = wrapper.findAll('.nv-savebar button').find((b) => b.text() === 'Discard')
    await discard.trigger('click')
    await flushPromises()
    // Confirmation first; Cancel is focused.
    const confirm = [...document.querySelectorAll('button')].find((b) => b.textContent.trim() === 'Discard changes')
    expect(confirm).toBeTruthy()
    confirm.click()
    await flushPromises()
    expect(wrapper.find('.nv-savebar').exists()).toBe(false)

    await wrapper.get('#settings-search').setValue('bitrate')
    expect(wrapper.find('#max_bitrate').exists()).toBe(true)
    expect(wrapper.find('#upnp').exists()).toBe(false)

    await wrapper.get('#settings-search').setValue('zzzz-no-match')
    expect(wrapper.text()).toContain('No settings match')
    wrapper.unmount()
  })

  it('shows errors on blur, and on Save shows all of them without saving', async () => {
    const wrapper = mountNova(Config)
    await flushPromises()
    const input = wrapper.get('#port input')
    await input.setValue('80')
    expect(wrapper.text()).not.toContain('Use 1029 or more.')
    await wrapper.get('#port').trigger('focusout', { relatedTarget: document.body })
    expect(wrapper.text()).toContain('Use 1029 or more.')

    const save = wrapper.findAll('.nv-savebar button').find((b) => b.text() === 'Save')
    await save.trigger('click')
    await flushPromises()
    expect(posted).toEqual([])
    expect(wrapper.text()).toContain('Fix the highlighted setting to save')
    wrapper.unmount()
  })
})

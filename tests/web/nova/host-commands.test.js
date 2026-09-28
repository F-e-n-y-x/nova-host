import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushPromises } from '@vue/test-utils'

import HostCommandsEditor from '../../../src_assets/common/assets/web/configs/components/HostCommandsEditor.vue'
import { commandLineError, hasInvalidCommand, HOST_COMMAND_ICONS, newHostCommand } from '../../../src_assets/common/assets/web/configs/components/hostCommands.js'
import { buildPayload, formFromApp, newAppForm, validateForm } from '../../../src_assets/common/assets/web/nova/pages/apps/appForm.js'
import { JSON_OPTIONS, prepareConfig } from '../../../src_assets/common/assets/web/configs/settings_model.js'
import { OPTIONS, SECTIONS } from '../../../src_assets/common/assets/web/configs/settings_schema.js'
import { mountNova } from './helpers.js'

/**
 * Stub fetch with per-route handlers ("METHOD /path" → [status, body]).
 *
 * @param {Record<string, (body: any) => [number, any]>} routes Handlers.
 * @returns {import('vitest').Mock} The stub.
 */
function stubHost(routes) {
  const stub = vi.fn(async (url, init = {}) => {
    const path = new URL(url, 'https://host/').pathname
    const key = `${(init.method || 'GET').toUpperCase()} ${path}`
    const handler = routes[key]
    if (!handler) return new Response(JSON.stringify({ error: `no route ${key}` }), { status: 404 })
    const [status, body] = handler(init.body ? JSON.parse(init.body) : undefined)
    return new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json' } })
  })
  vi.stubGlobal('fetch', stub)
  return stub
}

afterEach(() => {
  vi.unstubAllGlobals()
  vi.useRealTimers()
  document.body.innerHTML = ''
})

describe('host command helpers', () => {
  it('flags empty lines and unclosed quotes like the host does', () => {
    expect(commandLineError('')).toBe('empty')
    expect(commandLineError('   ')).toBe('empty')
    expect(commandLineError("echo 'open")).toBe('quote')
    expect(commandLineError('echo "a\\"b')).toBe('quote')
    expect(commandLineError("sh -c 'steam -shutdown; sleep 2'")).toBeNull()
    expect(commandLineError('echo "a\\"b"')).toBeNull()
    expect(commandLineError('echo don\\\'t')).toBeNull()
    expect(hasInvalidCommand([{ name: 'A', cmd: 'true' }])).toBe(false)
    expect(hasInvalidCommand([{ name: '', cmd: 'true' }])).toBe(true)
    expect(hasInvalidCommand(undefined)).toBe(false)
    expect(newHostCommand()).toEqual({ id: '', name: '', icon: 'terminal', cmd: '', confirm: false, timeout: 30 })
    expect(HOST_COMMAND_ICONS).toContain('refresh')
  })

  it('is a JSON option in the Host control section, Linux only', () => {
    expect(JSON_OPTIONS).toContain('host_commands')
    expect(OPTIONS.host_commands).toEqual({ type: 'HostCommandsEditor', platforms: ['linux'] })
    expect(OPTIONS.pcsleep_enabled.type).toBe('bool')
    expect(SECTIONS.find((s) => s.id === 'host').options).toEqual(['pcsleep_enabled', 'host_commands'])
    const { config } = prepareConfig({ platform: 'linux', host_commands: '[{"id":"a","name":"A","cmd":"true"}]' })
    expect(config.host_commands).toEqual([{ id: 'a', name: 'A', cmd: 'true' }])
  })

  it('keeps per-app commands in the app form as menu-cmd and drops blank rows on save', () => {
    const form = formFromApp({ name: 'Steam', cmd: 'steam', 'menu-cmd': [{ id: 'r', name: 'Restart', cmd: 'steam -shutdown' }] }, 0, 'linux')
    expect(form['menu-cmd'][0]).toEqual({ id: 'r', name: 'Restart', icon: 'terminal', cmd: 'steam -shutdown', confirm: false, timeout: 30 })
    form['menu-cmd'].push(newHostCommand())
    expect(validateForm(form).menuCmd).toBeUndefined()
    expect(buildPayload(form)['menu-cmd']).toHaveLength(1)

    form['menu-cmd'].push({ ...newHostCommand(), name: 'Broken', cmd: "echo 'x" })
    expect(validateForm(form).menuCmd).toBe('nova.apps.error_host_commands')

    const fresh = newAppForm()
    expect(fresh['menu-cmd']).toEqual([])
    expect('menu-cmd' in buildPayload({ ...fresh, name: 'X' })).toBe(false)
  })
})

describe('HostCommandsEditor', () => {
  it('adds, edits and removes rows through v-model', async () => {
    stubHost({
      'GET /api/host-commands': () => [200, { status: true, global: [], apps: [] }],
      'GET /api/host-commands/runs': () => [200, { status: true, runs: [] }],
    })
    let value = []
    const wrapper = mountNova(HostCommandsEditor, {
      props: { modelValue: value, 'onUpdate:modelValue': (v) => { value = v; wrapper.setProps({ modelValue: v }) } },
    })
    await flushPromises()
    expect(wrapper.text()).toContain('No host commands yet.')

    await wrapper.findAll('button').find((b) => b.text().includes('Add command')).trigger('click')
    expect(value).toHaveLength(1)
    // An empty row shows both validation messages.
    expect(wrapper.text()).toContain('Give it a name.')
    expect(wrapper.text()).toContain('close every quote')

    const inputs = wrapper.findAll('input[type="text"]')
    await inputs[0].setValue('Restart Steam')
    await wrapper.findAll('input[type="text"]')[1].setValue('steam -shutdown')
    expect(value[0]).toMatchObject({ name: 'Restart Steam', cmd: 'steam -shutdown', icon: 'terminal' })

    // Not saved yet: it can't run.
    const runButton = wrapper.findAll('button').find((b) => b.text().includes('Run now'))
    expect(runButton.attributes('disabled')).toBeDefined()

    await wrapper.find('button[aria-label^="Remove"]').trigger('click')
    expect(value).toEqual([])
    wrapper.unmount()
  })

  it('runs a saved command and shows its result and output', async () => {
    let runs = []
    const stub = stubHost({
      'GET /api/host-commands': () => [200, { status: true, global: [{ id: 'lock', name: 'Lock', cmd: 'loginctl lock-session' }], apps: [] }],
      'GET /api/host-commands/runs': () => [200, { status: true, runs }],
      'POST /api/host-commands/run': (body) => {
        runs = [{ id: body.id, running: false, ok: true, exit_code: 0, timed_out: false, duration_ms: 12, output: 'locked', error: '' }]
        return [200, { status: true, started: true }]
      },
    })
    vi.useFakeTimers({ shouldAdvanceTime: true })
    const wrapper = mountNova(HostCommandsEditor, {
      props: { modelValue: [{ id: 'lock', name: 'Lock', icon: 'lock', cmd: 'loginctl lock-session', confirm: true, timeout: 30 }] },
    })
    await flushPromises()
    const runButton = () => wrapper.findAll('button').find((b) => b.text().includes('Run now') || b.text().includes('Running'))
    expect(runButton().attributes('disabled')).toBeUndefined()
    expect(wrapper.text()).toContain('ID lock')

    await runButton().trigger('click')
    await flushPromises()
    const post = stub.mock.calls.find(([url, init]) => url.includes('/api/host-commands/run') && init?.method === 'POST')
    expect(JSON.parse(post[1].body)).toEqual({ id: 'lock', app: null })

    await vi.advanceTimersByTimeAsync(1100)
    await flushPromises()
    expect(wrapper.text()).toContain('Lock finished in 12 ms.')
    expect(wrapper.find('.nv-hostcmd__output pre').text()).toBe('locked')
    wrapper.unmount()
  })

  it('only offers Run for commands whose saved line is unchanged', async () => {
    stubHost({
      'GET /api/host-commands': () => [200, { status: true, global: [], apps: [{ index: 2, name: 'Steam', commands: [{ id: 'r', name: 'Restart', cmd: 'steam -shutdown' }] }] }],
      'GET /api/host-commands/runs': () => [200, { status: true, runs: [] }],
    })
    const wrapper = mountNova(HostCommandsEditor, {
      props: { appIndex: 2, modelValue: [{ id: 'r', name: 'Restart', icon: 'refresh', cmd: 'steam -shutdown --edited', confirm: false, timeout: 30 }] },
    })
    await flushPromises()
    const runButton = wrapper.findAll('button').find((b) => b.text().includes('Run now'))
    expect(runButton.attributes('disabled')).toBeDefined()
    expect(runButton.attributes('title')).toBe('Save first to run this command.')
    wrapper.unmount()
  })
})

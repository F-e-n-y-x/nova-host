import { afterEach, describe, expect, it, vi } from 'vitest'
import { flushPromises } from '@vue/test-utils'

import { mountNova } from './helpers.js'
import { MIN_PASSWORD_LENGTH, validateCredentials } from '../../../src_assets/common/assets/web/nova/pages/auth/useCredentialsForm.js'
import Welcome from '../../../src_assets/common/assets/web/Welcome.vue'
import Password from '../../../src_assets/common/assets/web/Password.vue'
import Logout from '../../../src_assets/common/assets/web/Logout.vue'
import ClientList from '../../../src_assets/common/assets/web/nova/pages/help/ClientList.vue'

const wrappers = []
const track = (w) => { wrappers.push(w); return w }

afterEach(() => {
  while (wrappers.length) wrappers.pop().unmount()
  document.body.innerHTML = ''
  vi.restoreAllMocks()
  vi.unstubAllGlobals()
  vi.useRealTimers()
})

const byLabel = (w, text) => {
  const label = w.findAll('label').find((l) => l.text() === text)
  return w.get(`#${label.attributes('for')}`)
}

describe('validateCredentials', () => {
  it('requires a username only when asked, a minimum length and matching confirmation', () => {
    const short = 'a'.repeat(MIN_PASSWORD_LENGTH - 1)
    expect(validateCredentials({ username: ' ', password: short, confirm: 'x' }, { usernameRequired: true }))
      .toEqual({
        username: { key: 'nova.auth.username_required' },
        password: { key: 'nova.auth.password_short', params: { min: MIN_PASSWORD_LENGTH } },
        confirm: { key: 'nova.auth.password_mismatch' },
      })
    const good = 'correct horse'
    expect(validateCredentials({ username: '', password: good, confirm: good }, { usernameRequired: false })).toEqual({})
  })
})

describe('Welcome', () => {
  it('starts empty, uses password-manager autocomplete, and validates on submit', async () => {
    vi.stubGlobal('fetch', vi.fn())
    const w = track(mountNova(Welcome))
    const username = byLabel(w, 'Username')
    expect(username.element.value).toBe('')
    expect(username.attributes('autocomplete')).toBe('username')
    expect(byLabel(w, 'Password').attributes('autocomplete')).toBe('new-password')
    expect(byLabel(w, 'Password').attributes('type')).toBe('password')
    expect(w.findAll('[aria-invalid="true"]')).toHaveLength(0)
    await w.get('form').trigger('submit')
    expect(w.findAll('[aria-invalid="true"]')).toHaveLength(2)
    expect(w.text()).toContain('Enter a username.')
    expect(fetch).not.toHaveBeenCalled()
  })

  it('reveals passwords on request', async () => {
    const w = track(mountNova(Welcome))
    await w.get('input[type="checkbox"]').setValue(true)
    expect(byLabel(w, 'Password').attributes('type')).toBe('text')
  })

  it('posts new credentials and shows the host error when it refuses', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: false, status: 400, clone() { return this }, json: async () => ({ status: false, error: 'Invalid Username' }) })))
    const w = track(mountNova(Welcome))
    await byLabel(w, 'Username').setValue('ayush')
    await byLabel(w, 'Password').setValue('long enough 1')
    await byLabel(w, 'Confirm password').setValue('long enough 1')
    await w.get('form').trigger('submit')
    await flushPromises()
    const [url, options] = fetch.mock.calls[0]
    expect(url).toBe('./api/password')
    expect(JSON.parse(options.body)).toEqual({ newUsername: 'ayush', newPassword: 'long enough 1', confirmNewPassword: 'long enough 1' })
    expect(w.get('[role="alert"]').text()).toContain('Invalid Username')
  })

  it('confirms success and offers to continue', async () => {
    vi.useFakeTimers()
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: true, status: 200, json: async () => ({ status: true }) })))
    const w = track(mountNova(Welcome))
    await byLabel(w, 'Username').setValue('ayush')
    await byLabel(w, 'Password').setValue('long enough 1')
    await byLabel(w, 'Confirm password').setValue('long enough 1')
    await w.get('form').trigger('submit')
    await flushPromises()
    expect(w.text()).toContain('Saved.')
    expect(w.find('form').exists()).toBe(false)
    expect(w.findAll('a').find((a) => a.text() === 'Continue').attributes('href')).toBe('./')
  })
})

describe('Password', () => {
  it('sends current and new credentials, keeping the username when left empty', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: true, status: 200, json: async () => ({ status: true }) })))
    vi.spyOn(globalThis, 'setTimeout').mockImplementation(() => 0)
    const w = track(mountNova(Password))
    await byLabel(w, 'Current username').setValue('ayush')
    await byLabel(w, 'Current password').setValue('old secret')
    await byLabel(w, 'New password').setValue('new secret 1')
    await byLabel(w, 'Confirm new password').setValue('new secret 1')
    expect(byLabel(w, 'Current password').attributes('autocomplete')).toBe('current-password')
    await w.get('form').trigger('submit')
    await flushPromises()
    expect(JSON.parse(fetch.mock.calls[0][1].body)).toEqual({
      currentUsername: 'ayush', currentPassword: 'old secret', newUsername: '', newPassword: 'new secret 1', confirmNewPassword: 'new secret 1',
    })
  })

  it('flags a mismatched confirmation after leaving the field', async () => {
    const w = track(mountNova(Password))
    await byLabel(w, 'New password').setValue('new secret 1')
    const confirm = byLabel(w, 'Confirm new password')
    await confirm.setValue('new secret 2')
    await confirm.trigger('focusout')
    expect(w.text()).toContain("The passwords don't match.")
    expect(confirm.attributes('aria-invalid')).toBe('true')
  })
})

describe('Logout', () => {
  it('offers to sign in again', () => {
    const w = track(mountNova(Logout))
    expect(w.get('h1').text()).toBe("You're signed out")
    expect(w.findAll('a').find((a) => a.text() === 'Sign in again').attributes('href')).toBe('./')
  })
})

describe('ClientList', () => {
  it('lists recommended apps statically without fetching', () => {
    vi.stubGlobal('fetch', vi.fn())
    const w = track(mountNova(ClientList))
    expect(w.findAll('.nv-clients__name').map((n) => n.text())).toEqual(['Nebula', 'Moonlight', 'Artemis', 'VoidLink'])
    expect(w.text()).toContain('Coming soon')
    expect(w.findAll('a[target="_blank"]').every((a) => a.attributes('rel') === 'noopener')).toBe(true)
    expect(fetch).not.toHaveBeenCalled()
  })
})

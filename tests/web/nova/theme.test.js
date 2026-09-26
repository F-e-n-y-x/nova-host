import { beforeEach, describe, expect, it, vi } from 'vitest'

import { THEME_KEY, applyTheme, getThemePreference, resolveTheme, setThemePreference } from '../../../src_assets/common/assets/web/theme.js'

function mockOsDark(dark) {
  globalThis.matchMedia = vi.fn().mockReturnValue({ matches: dark, addEventListener: vi.fn() })
}

describe('theme preference', () => {
  beforeEach(() => {
    localStorage.clear()
    delete document.documentElement.dataset.nvTheme
    mockOsDark(false)
  })

  it('defaults to following the system', () => {
    expect(getThemePreference()).toBe('system')
  })

  it('migrates legacy theme names to system', () => {
    localStorage.setItem('theme', 'dracula')
    expect(getThemePreference()).toBe('system')
    expect(localStorage.getItem('theme')).toBeNull()
  })

  it('ignores unknown saved values', () => {
    localStorage.setItem(THEME_KEY, 'zenith')
    expect(getThemePreference()).toBe('system')
  })

  it('applies an explicit choice as data attributes', () => {
    setThemePreference('dark')
    expect(localStorage.getItem(THEME_KEY)).toBe('dark')
    expect(document.documentElement.dataset.nvTheme).toBe('dark')
    expect(document.documentElement.dataset.bsTheme).toBe('dark')
  })

  it('resolves system from the OS and removes the attribute', () => {
    document.documentElement.dataset.nvTheme = 'light'
    mockOsDark(true)
    applyTheme('system')
    expect(document.documentElement.dataset.nvTheme).toBeUndefined()
    expect(document.documentElement.dataset.bsTheme).toBe('dark')
    expect(resolveTheme('system')).toBe('dark')
  })

  it('still works when storage throws', () => {
    const spy = vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => { throw new Error('blocked') })
    expect(() => setThemePreference('light')).not.toThrow()
    expect(document.documentElement.dataset.nvTheme).toBe('light')
    spy.mockRestore()
  })
})

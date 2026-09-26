import { beforeEach, describe, expect, it, vi } from 'vitest'

vi.mock('vue-i18n', () => ({
  createI18n: vi.fn(options => options),
}))

import { createI18n } from 'vue-i18n'
import createSunshineI18n, { applyConfiguredLocale, createInstantI18n, loadLocaleMessages } from '../../src_assets/common/assets/web/locale.js'

describe('locale initialization', () => {
  beforeEach(() => {
    createI18n.mockClear()
    document.documentElement.removeAttribute('lang')
  })

  it('loads a bundled configured locale', async () => {
    vi.stubGlobal('fetch', vi.fn()
      .mockResolvedValueOnce({ json: async () => ({ locale: 'de' }) })
      .mockResolvedValueOnce({ json: async () => ({ greeting: 'Hallo' }) }))

    const i18n = await createSunshineI18n()

    expect(fetch).toHaveBeenCalledWith('./api/configLocale')
    expect(fetch).toHaveBeenCalledWith('./assets/locale/de.json')
    expect(document.documentElement.lang).toBe('de')
    expect(i18n.locale).toBe('de')
    expect(i18n.fallbackLocale).toBe('en')
    expect(i18n.messages.en).toBeDefined()
    expect(i18n.messages.de).toEqual({ greeting: 'Hallo' })
  })

  it.each([
    ['an unsupported locale', 'unsupported'],
    ['a prototype property', '__proto__'],
    ['a non-string value', null],
  ])('falls back to English for %s', async (_description, locale) => {
    vi.stubGlobal('fetch', vi.fn(async () => ({
      json: async () => ({ locale }),
    })))

    const i18n = await createSunshineI18n()

    expect(document.documentElement.lang).toBe('en')
    expect(i18n.locale).toBe('en')
    expect(Object.keys(i18n.messages)).toEqual(['en'])
  })

  it('falls back to English when a bundled translation fails to load', async () => {
    const error = vi.spyOn(console, 'error').mockImplementation(() => {})
    const loadTranslation = async () => {
      throw new Error('translation unavailable')
    }

    const result = await loadLocaleMessages('de', loadTranslation)

    expect(result.locale).toBe('en')
    expect(Object.keys(result.messages)).toEqual(['en'])
    expect(error).toHaveBeenCalledWith('Failed to download translations', expect.any(Error))
    error.mockRestore()
  })

  it('creates an English instance synchronously without any request', () => {
    vi.stubGlobal('fetch', vi.fn())
    const i18n = createInstantI18n()

    expect(fetch).not.toHaveBeenCalled()
    expect(i18n.locale).toBe('en')
    expect(Object.keys(i18n.messages)).toEqual(['en'])
    expect(document.documentElement.lang).toBe('en')
  })

  function fakeI18n() {
    const messages = {}
    return { global: { locale: { value: 'en' }, setLocaleMessage: vi.fn((l, m) => { messages[l] = m }) }, messages }
  }

  it('switches a mounted instance to the configured locale', async () => {
    vi.stubGlobal('fetch', vi.fn()
      .mockResolvedValueOnce({ json: async () => ({ locale: 'de' }) })
      .mockResolvedValueOnce({ json: async () => ({ greeting: 'Hallo' }) }))
    const i18n = fakeI18n()

    await expect(applyConfiguredLocale(i18n)).resolves.toBe('de')
    expect(i18n.global.locale.value).toBe('de')
    expect(i18n.messages.de).toEqual({ greeting: 'Hallo' })
    expect(document.documentElement.lang).toBe('de')
  })

  it('stays in English when the host does not answer in time', async () => {
    vi.useFakeTimers()
    const error = vi.spyOn(console, 'error').mockImplementation(() => {})
    vi.stubGlobal('fetch', vi.fn((path, { signal }) => new Promise((resolve, reject) => {
      signal.addEventListener('abort', () => reject(new DOMException('aborted', 'AbortError')))
    })))
    const i18n = fakeI18n()

    const result = applyConfiguredLocale(i18n, 2000)
    await vi.advanceTimersByTimeAsync(2000)

    await expect(result).resolves.toBe('en')
    expect(i18n.global.locale.value).toBe('en')
    expect(i18n.global.setLocaleMessage).not.toHaveBeenCalled()
    error.mockRestore()
    vi.useRealTimers()
  })
})

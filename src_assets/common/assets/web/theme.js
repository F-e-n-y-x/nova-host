/**
 * @file Nova theme preference: "system" (default, follows the OS), "light" or "dark".
 *
 * The CSS in nova/nova.css reads `data-nv-theme` on <html>; with no attribute the
 * OS preference applies. `data-bs-theme` is kept in sync for pages that still use
 * Bootstrap components.
 */

export const THEME_KEY = 'nova-theme'
export const THEME_CHOICES = ['system', 'light', 'dark']

/** Key used by the Sunshine/Zenith multi-theme picker this module replaces. */
const LEGACY_THEME_KEY = 'theme'

const storage = {
  get(key) {
    try {
      return globalThis.localStorage?.getItem(key) ?? null
    } catch {
      return null
    }
  },
  set(key, value) {
    try {
      globalThis.localStorage?.setItem(key, value)
    } catch {
      // Private mode or blocked storage: the choice lasts for this page only.
    }
  },
  remove(key) {
    try {
      globalThis.localStorage?.removeItem(key)
    } catch {
      // Nothing to clean up when storage is unavailable.
    }
  },
}

let sessionChoice = null

const darkQuery = () => globalThis.matchMedia?.('(prefers-color-scheme: dark)')

/**
 * Read the saved preference, migrating any legacy theme name to "system".
 *
 * @returns {'system'|'light'|'dark'} The theme preference.
 */
export function getThemePreference() {
  if (storage.get(LEGACY_THEME_KEY) !== null) {
    storage.remove(LEGACY_THEME_KEY)
  }
  const saved = storage.get(THEME_KEY) ?? sessionChoice
  return THEME_CHOICES.includes(saved) ? saved : 'system'
}

/**
 * Resolve a preference to the palette actually shown.
 *
 * @param {string} [preference] Preference to resolve; defaults to the saved one.
 * @returns {'light'|'dark'} The effective theme.
 */
export function resolveTheme(preference = getThemePreference()) {
  if (preference === 'light' || preference === 'dark') {
    return preference
  }
  return darkQuery()?.matches ? 'dark' : 'light'
}

/**
 * Apply a preference to the document.
 *
 * @param {string} [preference] Preference to apply; defaults to the saved one.
 */
export function applyTheme(preference = getThemePreference()) {
  const root = document.documentElement
  if (preference === 'light' || preference === 'dark') {
    root.dataset.nvTheme = preference
  } else {
    delete root.dataset.nvTheme
  }
  root.dataset.bsTheme = resolveTheme(preference)
}

/**
 * Save and apply a preference.
 *
 * @param {'system'|'light'|'dark'} preference The new preference.
 */
export function setThemePreference(preference) {
  const value = THEME_CHOICES.includes(preference) ? preference : 'system'
  sessionChoice = value
  storage.set(THEME_KEY, value)
  applyTheme(value)
}

/**
 * Apply the saved theme and follow OS changes while "system" is selected.
 */
export function loadAutoTheme() {
  applyTheme()
  darkQuery()?.addEventListener?.('change', () => {
    if (getThemePreference() === 'system') {
      applyTheme('system')
    }
  })
}

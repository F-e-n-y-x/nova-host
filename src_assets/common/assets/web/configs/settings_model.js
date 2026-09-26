/**
 * @file Pure helpers for the Settings page: value comparison, which options apply,
 * search, validation and the save payload. No Vue or DOM here, so it is unit-testable.
 */
import configTabs from './config_tabs.json'
import { ENCODER_GROUPS, OPTIONS, SECTIONS, isOn } from './settings_schema.js'

/**
 * Deep copy of config data. Values are plain JSON; unlike structuredClone this also
 * accepts Vue reactive proxies.
 *
 * @param {*} value Value to copy.
 * @returns {*} Copy.
 */
export function clone(value) {
  return value === undefined ? undefined : JSON.parse(JSON.stringify(value))
}

/** Keys /api/config returns that are not settings and must never be written back. */
export const RESPONSE_ONLY_KEYS = ['platform', 'status', 'version', 'username']

/** Options stored as JSON text in the config file. */
export const JSON_OPTIONS = ['dd_mode_remapping', 'global_prep_cmd']

/** Defaults for every option, from config_tabs.json. */
export const DEFAULTS = Object.freeze(Object.fromEntries(
  configTabs.flatMap((tab) => Object.entries(tab.options)),
))

/** Config-tab id each option belongs to (used to hide encoder options of other platforms). */
export const OPTION_TAB = Object.freeze(Object.fromEntries(
  configTabs.flatMap((tab) => Object.keys(tab.options).map((key) => [key, tab.id])),
))

const BOOL_PAIRS = [['true', 'false'], ['1', '0'], ['enabled', 'disabled'], ['enable', 'disable'], ['yes', 'no'], ['on', 'off']]

/**
 * The on/off spelling a boolean option uses, taken from its current value or default,
 * so a toggle writes back the same representation the config file already has.
 *
 * @param {*} value Current value.
 * @param {*} fallback Default value.
 * @returns {[*, *]} [onValue, offValue].
 */
export function boolRepresentation(value, fallback) {
  for (const candidate of [value, fallback]) {
    if (candidate === true || candidate === false) return [true, false]
    if (candidate === 1 || candidate === 0) return [1, 0]
    const text = `${candidate ?? ''}`.toLowerCase().trim()
    const pair = BOOL_PAIRS.find((p) => p.includes(text))
    if (pair) return pair
  }
  return ['enabled', 'disabled']
}

/**
 * Normalize a value for comparison: config files store everything as text, while the
 * defaults mix numbers, strings and booleans.
 *
 * @param {*} value Value to normalize.
 * @param {string} key Option name (its schema type decides boolean handling).
 * @returns {string} Comparable form.
 */
function comparable(value, key) {
  if (value !== null && typeof value === 'object') return JSON.stringify(value)
  const text = `${value ?? ''}`.trim()
  if (OPTIONS[key]?.type === 'bool' && text !== '') return isOn(text.toLowerCase()) ? 'true' : 'false'
  if (text !== '' && !Number.isNaN(Number(text)) && typeof DEFAULTS[key] === 'number') return String(Number(text))
  return text
}

/**
 * Whether two values of one option mean the same thing.
 *
 * @param {*} a First value.
 * @param {*} b Second value.
 * @param {string} key Option name.
 * @returns {boolean} True when equivalent.
 */
export function sameValue(a, b, key) {
  return comparable(a, key) === comparable(b, key)
}

/**
 * Whether an option differs from its default.
 *
 * @param {string} key Option name.
 * @param {*} value Current value.
 * @returns {boolean} True when changed from the default.
 */
export function isModified(key, value) {
  const fallback = DEFAULTS[key]
  // An empty value means "not set", which is the default for options without one.
  if ((value === '' || value === null || value === undefined) && (fallback === '' || fallback === undefined)) return false
  return !sameValue(value, fallback, key)
}

/**
 * Turn the /api/config response into the page's working copy: response-only keys
 * removed, JSON options parsed, and every missing option filled with its default.
 *
 * @param {object} response Parsed /api/config body.
 * @returns {{config: object, platform: string}} Working configuration and platform.
 */
export function prepareConfig(response) {
  const config = { ...response }
  const platform = config.platform || ''
  for (const key of RESPONSE_ONLY_KEYS) delete config[key]
  for (const key of JSON_OPTIONS) {
    if (typeof config[key] === 'string') {
      try {
        config[key] = JSON.parse(config[key])
      } catch {
        config[key] = clone(DEFAULTS[key])
      }
    }
  }
  for (const [key, value] of Object.entries(DEFAULTS)) {
    if (config[key] === undefined) config[key] = clone(value)
  }
  return { config, platform }
}

/**
 * The body for POST /api/config. The host rewrites the whole file from it, so it must
 * hold every setting that differs from its default — not just the ones edited now —
 * and nothing that is only part of the GET response.
 *
 * @param {object} config Working configuration.
 * @returns {object} Keys to save.
 */
export function buildSavePayload(config) {
  const payload = {}
  for (const [key, value] of Object.entries(config)) {
    if (RESPONSE_ONLY_KEYS.includes(key)) continue
    if (key in DEFAULTS && !isModified(key, value)) continue
    if (value === '' || value === null || value === undefined) continue
    payload[key] = clone(value)
  }
  return payload
}

/**
 * Keys whose value differs between two configurations.
 *
 * @param {object} current Working configuration.
 * @param {object} saved Last saved configuration.
 * @returns {string[]} Changed keys.
 */
export function changedKeys(current, saved) {
  const keys = new Set([...Object.keys(current), ...Object.keys(saved)])
  return [...keys].filter((key) => !sameValue(current[key], saved[key], key))
}

/**
 * Whether an option is shown on this platform with the current values.
 *
 * @param {string} key Option name.
 * @param {object} config Working configuration.
 * @param {string} platform Host platform.
 * @returns {boolean} True when it applies.
 */
export function optionApplies(key, config, platform) {
  const option = OPTIONS[key]
  if (!option) return false
  if (option.platforms && !option.platforms.includes(platform)) return false
  if (option.hideOn?.includes(platform)) return false
  if (option.when && !option.when(config, platform)) return false
  return true
}

/**
 * Encoder groups available on a platform.
 *
 * @param {string} platform Host platform.
 * @returns {typeof ENCODER_GROUPS} Groups in display order.
 */
export function encoderGroupsFor(platform) {
  return ENCODER_GROUPS.filter((g) => (!g.platforms || g.platforms.includes(platform)) && !g.hideOn?.includes(platform))
}

/** Config keys belonging to an encoder group (from its config tab). */
export function encoderGroupKeys(groupId) {
  return Object.keys(OPTION_TAB).filter((key) => OPTION_TAB[key] === groupId)
}

/**
 * Which encoder groups to show expanded: the one chosen in `encoder`, else the ones the
 * host found at startup, else all of them (nothing known yet).
 *
 * @param {object} config Working configuration.
 * @param {string[]} detected Encoder ids from the log ("nvenc", "vaapi", …).
 * @param {string} platform Host platform.
 * @returns {Set<string>} Group ids.
 */
export function primaryEncoderGroups(config, detected, platform) {
  const groups = encoderGroupsFor(platform)
  const wanted = config.encoder ? [config.encoder] : detected
  const matched = groups.filter((g) => g.encoders.some((e) => wanted.includes(e))).map((g) => g.id)
  return new Set(matched.length ? matched : groups.map((g) => g.id))
}

/**
 * Case-insensitive match of a search query against an option's key, label and text.
 *
 * @param {string} query Search text.
 * @param {string} key Option name.
 * @param {string[]} texts Translated label, description and section title.
 * @returns {boolean} True when every word of the query matches.
 */
export function matchesSearch(query, key, texts) {
  const words = query.trim().toLowerCase().split(/\s+/).filter(Boolean)
  if (words.length === 0) return true
  const haystack = [key, key.replaceAll('_', ' '), ...texts].join(' ').toLowerCase()
  return words.every((word) => haystack.includes(word))
}

/**
 * Validation error (an i18n key) for one option, or null.
 *
 * @param {string} key Option name.
 * @param {*} value Current value.
 * @returns {{key: string, params?: object}|null} Error message key and parameters.
 */
export function validateOption(key, value) {
  const option = OPTIONS[key]
  if (!option) return null
  if (option.type === 'number' && value !== '' && value !== null && value !== undefined) {
    const n = Number(value)
    if (Number.isNaN(n)) return { key: 'nova.settings.errors.number' }
    if (option.integer && !Number.isInteger(n)) return { key: 'nova.settings.errors.integer' }
    if (option.min !== undefined && n < option.min) return { key: 'nova.settings.errors.min', params: { min: option.min } }
    if (option.max !== undefined && n > option.max) return { key: 'nova.settings.errors.max', params: { max: option.max } }
  }
  const custom = option.validate?.(value ?? '')
  return custom ? { key: custom } : null
}

/** Every option key the page renders, in section order (encoder groups included). */
export function allOptionKeys() {
  const keys = SECTIONS.flatMap((s) => s.options)
  for (const group of ENCODER_GROUPS) keys.push(...encoderGroupKeys(group.id))
  return keys
}

/**
 * Split a long description into a short lead and the rest, at the first sentence end
 * after `limit` characters' worth of text is not reached.
 *
 * @param {string} text Description.
 * @param {number} [limit] Longest lead before splitting.
 * @returns {{lead: string, rest: string}} Parts.
 */
export function splitDescription(text, limit = 140) {
  if (!text || text.length <= limit) return { lead: text || '', rest: '' }
  const match = /^(.+?[.!?])\s+(.+)$/s.exec(text)
  if (!match || match[1].length > limit * 1.6) return { lead: text, rest: '' }
  return { lead: match[1], rest: match[2] }
}

/**
 * @file Pure helpers for showing devices: IDs, times, permission summaries, validation.
 */

/** Permission flags in display order, matching the host's JSON keys. */
export const PERMISSION_FLAGS = ['input_keyboard', 'input_mouse', 'input_controller', 'input_touch_pen', 'clipboard', 'launch_apps', 'power', 'host_commands']

/** Flags that control the host itself; off unless granted, even for devices saved before they existed. */
export const HOST_CONTROL_FLAGS = ['power', 'host_commands']

/** Presets the host understands, in display order. */
export const PERMISSION_PRESETS = ['full', 'standard', 'play', 'view_only']

/** Flags each preset turns on; the rest are off. */
export const PRESET_FLAGS = {
  full: PERMISSION_FLAGS,
  standard: ['input_keyboard', 'input_mouse', 'input_controller', 'input_touch_pen', 'clipboard', 'launch_apps'],
  play: ['input_keyboard', 'input_mouse', 'input_controller', 'input_touch_pen', 'launch_apps'],
  view_only: [],
}

/**
 * Permission flags as booleans. A missing flag counts as allowed, like the host does, except the
 * host-control flags, which count as denied.
 *
 * @param {object|undefined} permissions Permission object from the host.
 * @returns {Record<string, boolean>} Flag name → allowed.
 */
export function permissionFlags(permissions) {
  const source = permissions || {}
  return Object.fromEntries(PERMISSION_FLAGS.map((flag) => [flag, HOST_CONTROL_FLAGS.includes(flag) ? source[flag] === true : source[flag] !== false]))
}

/**
 * The preset a set of flags matches.
 *
 * @param {Record<string, boolean>} flags Flag name → allowed.
 * @returns {'full'|'standard'|'play'|'view_only'|'custom'} Preset name.
 */
export function presetForFlags(flags) {
  const on = PERMISSION_FLAGS.filter((f) => flags[f])
  const match = PERMISSION_PRESETS.find((p) => PRESET_FLAGS[p].length === on.length && PRESET_FLAGS[p].every((f) => flags[f]))
  return match || 'custom'
}

/**
 * The flags a preset turns on.
 *
 * @param {string} preset Preset name.
 * @returns {Record<string, boolean>} Flag name → allowed.
 */
export function flagsForPreset(preset) {
  return Object.fromEntries(PERMISSION_FLAGS.map((f) => [f, (PRESET_FLAGS[preset] || []).includes(f)]))
}

/**
 * First and last characters of a device ID.
 *
 * @param {string} uuid Device ID.
 * @returns {string} Shortened ID, or the whole ID when it is short.
 */
export function shortId(uuid) {
  if (!uuid || uuid.length <= 13) return uuid || ''
  return `${uuid.slice(0, 8)}…${uuid.slice(-4)}`
}

const UNITS = [
  ['year', 365 * 24 * 3600],
  ['month', 30 * 24 * 3600],
  ['week', 7 * 24 * 3600],
  ['day', 24 * 3600],
  ['hour', 3600],
  ['minute', 60],
]

/**
 * Relative time such as "5 minutes ago".
 *
 * @param {number|null} unixSeconds Time in Unix seconds.
 * @param {string} locale BCP 47 locale.
 * @param {number} [nowMs] Current time in milliseconds (for tests).
 * @returns {string} Formatted text, or '' when the time is unknown.
 */
export function relativeTime(unixSeconds, locale, nowMs = Date.now()) {
  if (!unixSeconds) return ''
  const diff = unixSeconds - Math.round(nowMs / 1000)
  const format = new Intl.RelativeTimeFormat(locale, { numeric: 'auto' })
  for (const [unit, seconds] of UNITS) {
    if (Math.abs(diff) >= seconds) return format.format(Math.round(diff / seconds), unit)
  }
  return format.format(0, 'minute')
}

/**
 * Full date and time, for tooltips and detail views.
 *
 * @param {number|null} unixSeconds Time in Unix seconds.
 * @param {string} locale BCP 47 locale.
 * @returns {string} Formatted text, or '' when the time is unknown.
 */
export function absoluteTime(unixSeconds, locale) {
  if (!unixSeconds) return ''
  return new Intl.DateTimeFormat(locale, { dateStyle: 'medium', timeStyle: 'short' }).format(new Date(unixSeconds * 1000))
}

/**
 * The preset a permission object matches.
 *
 * @param {object|undefined} permissions Permission object from the host.
 * @returns {'full'|'standard'|'play'|'view_only'|'custom'} Preset name; missing data counts as
 *   standard access, which is what the host applies to devices without stored permissions.
 */
export function permissionPreset(permissions) {
  if (!permissions) return 'standard'
  if (PERMISSION_PRESETS.includes(permissions.preset)) return permissions.preset
  return 'custom'
}

/**
 * The live session belonging to a device, if it is streaming.
 *
 * @param {Array<object>} sessions Sessions from /api/sessions.
 * @param {string} uuid Device ID.
 * @returns {object|null} The session, or null.
 */
export function sessionFor(sessions, uuid) {
  return (sessions || []).find((s) => s.client_uuid && s.client_uuid === uuid) || null
}

/**
 * Check a device name the way the host does.
 *
 * @param {string} name Candidate name.
 * @returns {boolean} Whether it has 1–64 characters, no surrounding spaces and no control characters.
 */
export function isValidDeviceName(name) {
  if (!name || name !== name.trim()) return false
  // eslint-disable-next-line no-control-regex
  if (/[\u0000-\u001f\u007f]/.test(name)) return false
  return [...name].length <= 64
}

/**
 * Filter devices by a search string (name or ID, case-insensitive).
 *
 * @param {Array<object>} devices Devices.
 * @param {string} query Search text.
 * @returns {Array<object>} Matching devices.
 */
export function filterDevices(devices, query) {
  const q = (query || '').trim().toLowerCase()
  if (!q) return devices
  return devices.filter((d) => (d.name || '').toLowerCase().includes(q) || (d.uuid || '').toLowerCase().includes(q))
}

/**
 * Sort devices: streaming first, then by most recent connection, then by name.
 *
 * @param {Array<object>} devices Devices.
 * @returns {Array<object>} A sorted copy.
 */
export function sortDevices(devices) {
  return [...devices].sort((a, b) => {
    if (!!a.connected !== !!b.connected) return a.connected ? -1 : 1
    const last = (b.last_connected_at || 0) - (a.last_connected_at || 0)
    if (last !== 0) return last
    return (a.name || '').localeCompare(b.name || '')
  })
}

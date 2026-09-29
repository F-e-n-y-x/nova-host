/**
 * @file Pure helpers for the application editor: defaults, loading an app into an
 * editable form, validation and building the save payload for POST /api/apps.
 */
import { hasInvalidCommand, newHostCommand } from '../../../configs/components/hostCommands.js'

/**
 * Host-command rows with anything typed in them (fully empty rows are dropped on save).
 *
 * @param {Array<object>|undefined} list Rows.
 * @returns {Array<object>} Non-blank rows.
 */
function dropBlankCommands(list) {
  return (Array.isArray(list) ? list : []).filter((c) => String(c.name ?? '').trim() || String(c.cmd ?? '').trim())
}

/**
 * Deep copy of JSON app data. Works on Vue reactive proxies, which structuredClone rejects.
 *
 * @param {object} value JSON-compatible value.
 * @returns {object} Copy.
 */
function clone(value) {
  return JSON.parse(JSON.stringify(value))
}

/**
 * Whether an app runs a Windows game through Nova's compatibility layer (not Lutris, not a native app).
 *
 * @param {object} app Application or form.
 * @returns {boolean} True for Windows games Nova launches itself.
 */
export function isWindowsGame(app) {
  const cmd = String(app?.cmd ?? '')
  if (/lutris:rungameid/i.test(cmd) || app?.['nova-source'] === 'lutris') return false
  return Boolean(app?.['nova-exe']) || /nova-proton-run/.test(cmd) || /\.exe"?\s*$/i.test(cmd)
}

/**
 * Editable copy of an app's `nova-compat` options (see docs/configuration.md, windows_launcher).
 * FSR, the frame rate limit and MangoHud moved to the performance profile (perfForm()).
 *
 * @param {object} [compat] Stored `nova-compat` object.
 * @returns {{prefix: string, proton_version: string, extra_env: string}} Form state.
 */
export function compatForm(compat = {}) {
  return {
    prefix: compat.prefix ?? '',
    proton_version: compat.proton_version ?? '',
    extra_env: (compat.extra_env ?? []).join('\n'),
  }
}

/**
 * Turn the editable options back into the stored `nova-compat` object.
 *
 * @param {object} form Output of compatForm().
 * @returns {object|null} Stored object, or null when every option is at its default.
 */
export function compatPayload(form) {
  const out = {}
  const prefix = String(form.prefix ?? '').trim()
  if (prefix) out.prefix = prefix
  const version = String(form.proton_version ?? '').trim()
  if (version && version !== 'latest') out.proton_version = version
  const env = String(form.extra_env ?? '').split('\n').map((l) => l.trim()).filter((l) => /^[A-Za-z_][A-Za-z0-9_]*=/.test(l))
  if (env.length) out.extra_env = env
  return Object.keys(out).length ? out : null
}

/** Limits of the performance profile, matching src/nova_perf.h. */
export const PERF_LIMITS = { fpsCap: 1000, fsr: 5, minBitrateMbps: 0.5, maxBitrateMbps: 800, defaultCas: 50 }

/** Power modes a profile can ask for ('default' follows the host's power mode setting). */
export const PERF_POWER_MODES = ['default', 'performance', 'balanced']

/**
 * How the host starts an app, like src/nova_perf.cpp launcher_of(). Launch variables (frame rate
 * limit, FSR, sharpening, overlay) don't reach games Steam starts; the stream settings apply anyway.
 *
 * @param {object} app Application or form.
 * @returns {'command'|'proton'|'steam'|'lutris'|'none'} Launcher kind.
 */
export function perfLauncher(app) {
  const cmd = String(app?.cmd ?? '')
  const lower = cmd.toLowerCase()
  if (lower.includes('lutris:') || app?.['nova-source'] === 'lutris') return 'lutris'
  if (lower.includes('steam://')) return 'steam'
  if (app?.['nova-exe'] || lower.includes('nova-proton-run')) return 'proton'
  if (!cmd.trim()) return 'none'
  return 'command'
}

/**
 * Editable copy of an app's performance profile (`nova-perf`, see docs/configuration.md). Apps
 * saved before it existed keep the FSR, frame rate limit and MangoHud options `nova-compat` held.
 *
 * @param {object} [app] Stored application.
 * @returns {{fps_cap: number|null, fsr: string, vkbasalt: boolean, vkbasalt_cas: number, mangohud: boolean, bitrate_mbps: number|null, power: string}} Form state.
 */
export function perfForm(app = {}) {
  const stored = app?.['nova-perf'] && typeof app['nova-perf'] === 'object'
    ? app['nova-perf']
    : (({ fsr, fps_cap, mangohud }) => ({ fsr, fps_cap, mangohud }))(app?.['nova-compat'] ?? {})
  const kbps = Number(stored.bitrate_kbps)
  return {
    fps_cap: Number(stored.fps_cap) > 0 ? Number(stored.fps_cap) : null,
    fsr: String(Number(stored.fsr) >= 1 && Number(stored.fsr) <= PERF_LIMITS.fsr ? Number(stored.fsr) : 0),
    vkbasalt: stored.vkbasalt === true,
    vkbasalt_cas: Number.isFinite(Number(stored.vkbasalt_cas)) && stored.vkbasalt_cas !== undefined ? Number(stored.vkbasalt_cas) : PERF_LIMITS.defaultCas,
    mangohud: stored.mangohud === true,
    bitrate_mbps: kbps > 0 ? kbps / 1000 : null,
    power: PERF_POWER_MODES.includes(stored.power) ? stored.power : 'default',
  }
}

/**
 * Turn the editable profile back into the stored `nova-perf` object (defaults left out).
 *
 * @param {object} form Output of perfForm().
 * @returns {object|null} Stored object, or null when every setting is at its default.
 */
export function perfPayload(form) {
  const out = {}
  const fps = Number(form?.fps_cap)
  if (Number.isInteger(fps) && fps > 0) out.fps_cap = Math.min(fps, PERF_LIMITS.fpsCap)
  const fsr = Number(form?.fsr)
  if (Number.isInteger(fsr) && fsr >= 1 && fsr <= PERF_LIMITS.fsr) out.fsr = fsr
  if (form?.vkbasalt) out.vkbasalt = true
  const cas = Math.round(Number(form?.vkbasalt_cas))
  if (Number.isFinite(cas) && cas !== PERF_LIMITS.defaultCas) out.vkbasalt_cas = Math.min(100, Math.max(0, cas))
  if (form?.mangohud) out.mangohud = true
  const mbps = Number(form?.bitrate_mbps)
  if (form?.bitrate_mbps !== null && form?.bitrate_mbps !== '' && Number.isFinite(mbps) && mbps > 0) {
    out.bitrate_kbps = Math.round(Math.min(PERF_LIMITS.maxBitrateMbps, Math.max(PERF_LIMITS.minBitrateMbps, mbps)) * 1000)
  }
  if (form?.power === 'performance' || form?.power === 'balanced') out.power = form.power
  return Object.keys(out).length ? out : null
}

/**
 * Short summary of a stored profile for cards ("60 fps · FSR 2 · 40 Mbps").
 *
 * @param {object} app Application.
 * @returns {string[]} Parts; empty when the app has no profile.
 */
export function perfSummary(app) {
  const p = perfPayload(perfForm(app)) || {}
  const parts = []
  if (p.fps_cap) parts.push(`${p.fps_cap} fps`)
  if (p.fsr) parts.push(`FSR ${p.fsr}`)
  if (p.vkbasalt) parts.push('CAS')
  if (p.bitrate_kbps) parts.push(`${p.bitrate_kbps / 1000} Mbps`)
  return parts
}

/** Seconds the host waits for app processes to exit when no timeout is stored. */
export const DEFAULT_EXIT_TIMEOUT = 5

/**
 * A blank form for a new application, matching the fields the host stores.
 *
 * @returns {object} New form state (index -1 means "add").
 */
export function newAppForm() {
  return {
    index: -1,
    name: '',
    output: '',
    cmd: '',
    'working-dir': '',
    'exclude-global-prep-cmd': false,
    elevated: false,
    'auto-detach': true,
    'wait-all': true,
    'exit-timeout': DEFAULT_EXIT_TIMEOUT,
    'prep-cmd': [],
    'menu-cmd': [],
    detached: [],
    'image-path': '',
    'nova-perf': perfForm(),
  }
}

/**
 * Copy an existing application into an editable form. Unknown keys are kept so
 * saving never drops fields this UI doesn't show.
 *
 * @param {object} app Application as returned by GET /api/apps.
 * @param {number} index Its position in the apps list.
 * @param {string} platform Host platform ('windows', 'linux', …).
 * @returns {object} Form state.
 */
export function formFromApp(app, index, platform) {
  const form = clone(app)
  form.index = index
  form.name = form.name ?? ''
  form.cmd = form.cmd ?? ''
  form.output = form.output ?? ''
  form['working-dir'] = form['working-dir'] ?? ''
  form['image-path'] = form['image-path'] ?? ''
  form['prep-cmd'] = (form['prep-cmd'] ?? []).map((c) => ({ ...c, do: c.do ?? '', undo: c.undo ?? '' }))
  form.detached = [...(form.detached ?? [])]
  form['exclude-global-prep-cmd'] = form['exclude-global-prep-cmd'] ?? false
  form['menu-cmd'] = (Array.isArray(form['menu-cmd']) ? form['menu-cmd'] : []).map((c) => ({ ...newHostCommand(), ...c }))
  if (form.elevated === undefined && platform === 'windows') form.elevated = false
  form['auto-detach'] = form['auto-detach'] ?? true
  form['wait-all'] = form['wait-all'] ?? true
  form['exit-timeout'] = form['exit-timeout'] ?? DEFAULT_EXIT_TIMEOUT
  form['nova-perf'] = perfForm(app)
  if (isWindowsGame(form)) form['nova-compat'] = compatForm(form['nova-compat'])
  return form
}

/**
 * A new prep-command row.
 *
 * @param {string} platform Host platform; Windows rows carry an `elevated` flag.
 * @returns {{do: string, undo: string, elevated?: boolean}} The row.
 */
export function newPrepCmd(platform) {
  return platform === 'windows' ? { do: '', undo: '', elevated: false } : { do: '', undo: '' }
}

/**
 * Validate a form.
 *
 * @param {object} form Form state.
 * @returns {{name?: string, exitTimeout?: string}} Error keys by field (empty when valid).
 */
export function validateForm(form) {
  const errors = {}
  if (!String(form.name ?? '').trim()) errors.name = 'nova.apps.error_name_required'
  if (hasInvalidCommand(dropBlankCommands(form['menu-cmd']))) errors.menuCmd = 'nova.apps.error_host_commands'
  const timeout = form['exit-timeout']
  if (timeout !== null && timeout !== undefined && timeout !== '' &&
      (!Number.isInteger(Number(timeout)) || Number(timeout) < 0)) {
    errors.exitTimeout = 'nova.apps.error_exit_timeout'
  }
  const perf = form['nova-perf']
  const blank = (v) => v === null || v === undefined || v === ''
  if (perf && !blank(perf.fps_cap) && (!Number.isInteger(Number(perf.fps_cap)) || Number(perf.fps_cap) < 0 || Number(perf.fps_cap) > PERF_LIMITS.fpsCap)) {
    errors.perfFps = 'nova.apps.perf_error_fps'
  }
  if (perf && !blank(perf.bitrate_mbps) && Number(perf.bitrate_mbps) !== 0 &&
      !(Number(perf.bitrate_mbps) >= PERF_LIMITS.minBitrateMbps && Number(perf.bitrate_mbps) <= PERF_LIMITS.maxBitrateMbps)) {
    errors.perfBitrate = 'nova.apps.perf_error_bitrate'
  }
  return errors
}

/**
 * Build the JSON body for POST /api/apps.
 * Same shape the previous editor sent (the full app plus `index`), with blank
 * detached/prep rows dropped, quotes stripped from the image path, and an empty exit
 * timeout omitted so the host's default applies instead of storing "".
 *
 * @param {object} form Form state.
 * @returns {object} Payload.
 */
export function buildPayload(form) {
  const payload = clone(form)
  payload.name = String(payload.name ?? '').trim()
  payload['image-path'] = String(payload['image-path'] ?? '').replaceAll('"', '')
  payload.detached = (payload.detached ?? []).filter((c) => String(c).trim() !== '')
  payload['prep-cmd'] = (payload['prep-cmd'] ?? []).filter((c) => String(c.do ?? '').trim() || String(c.undo ?? '').trim())
  const commands = dropBlankCommands(payload['menu-cmd']).map((c) => ({ ...c, name: String(c.name ?? '').trim(), cmd: String(c.cmd ?? '').trim() }))
  if (commands.length) payload['menu-cmd'] = commands
  else delete payload['menu-cmd']
  const timeout = payload['exit-timeout']
  if (timeout === null || timeout === undefined || timeout === '') {
    delete payload['exit-timeout']
  } else {
    payload['exit-timeout'] = Number(timeout)
  }
  if (payload['nova-compat']) {
    const compat = compatPayload(payload['nova-compat'])
    if (compat) payload['nova-compat'] = compat
    else delete payload['nova-compat']
  }
  if (payload['nova-perf']) {
    const perf = perfPayload(payload['nova-perf'])
    if (perf) payload['nova-perf'] = perf
    else delete payload['nova-perf']
  }
  return payload
}

/**
 * Whether two forms differ (used to confirm before discarding edits).
 *
 * @param {object} a First form.
 * @param {object} b Second form.
 * @returns {boolean} True when they differ.
 */
export function formsDiffer(a, b) {
  return JSON.stringify(a) !== JSON.stringify(b)
}

/**
 * Short labels describing an app's non-default behaviour, for cards and rows.
 *
 * @param {object} app Application.
 * @returns {string[]} i18n keys.
 */
export function appFlags(app) {
  const flags = []
  if (app.elevated) flags.push('nova.apps.flag_admin')
  if (app.detached?.length) flags.push('nova.apps.flag_detached')
  if (app['prep-cmd']?.length) flags.push('nova.apps.flag_prep')
  if (app['exclude-global-prep-cmd']) flags.push('nova.apps.flag_no_global_prep')
  if (perfPayload(perfForm(app))) flags.push('nova.apps.flag_perf')
  return flags
}

/**
 * Filter and sort apps for display, keeping each app's original index (the host API
 * addresses apps by index).
 *
 * @param {object[]} apps Apps from the host.
 * @param {string} query Search text.
 * @param {'default'|'asc'|'desc'} sort Sort mode.
 * @returns {{app: object, index: number}[]} Visible apps.
 */
export function visibleApps(apps, query, sort) {
  let list = (apps ?? []).map((app, index) => ({ app, index }))
  const q = String(query ?? '').trim().toLowerCase()
  if (q) {
    list = list.filter(({ app }) => (app.name || '').toLowerCase().includes(q) || (app.cmd || '').toLowerCase().includes(q))
  }
  if (sort === 'asc' || sort === 'desc') {
    list.sort((a, b) => {
      const r = (a.app.name || '').localeCompare(b.app.name || '', undefined, { sensitivity: 'base' })
      return sort === 'asc' ? r : -r
    })
  }
  return list
}

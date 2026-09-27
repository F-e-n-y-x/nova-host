/**
 * @file Pure formatting helpers for the Overview page (tested in tests/web/nova/overview.test.js).
 */

const ENCODERS = { nvenc: 'NVENC', vaapi: 'VA-API', vulkan: 'Vulkan', quicksync: 'Quick Sync', qsv: 'Quick Sync',
  amdvce: 'AMF', amf: 'AMF', videotoolbox: 'VideoToolbox', software: 'Software' }
const CAPTURES = { nvfbc: 'NvFBC', kms: 'KMS', x11: 'X11', wlr: 'wlroots', portal: 'Portal', ddx: 'DXGI', wgc: 'WGC' }

/**
 * Display name for an encoder id from /api/host/info.
 *
 * @param {string} id e.g. "nvenc".
 * @returns {string}
 */
export function encoderLabel(id) {
  if (!id) return ''
  return ENCODERS[String(id).toLowerCase()] || String(id).toUpperCase()
}

/**
 * Display name for a capture method id.
 *
 * @param {string} id e.g. "nvfbc".
 * @returns {string}
 */
export function captureLabel(id) {
  if (!id) return ''
  return CAPTURES[String(id).toLowerCase()] || id
}

/**
 * Release part of a version ("0.1.0-59dad173-dirty" → "0.1.0").
 *
 * @param {string} version Host version string.
 * @returns {string}
 */
export function shortVersion(version) {
  return String(version || '').replace(/^nova-v|^v/, '').split(/[-+]/)[0]
}

/**
 * Megabits per second from kilobits, rounded for display ("30 Mbps").
 *
 * @param {number} kbps Kilobits per second.
 * @returns {string}
 */
export function formatMbps(kbps) {
  if (!Number.isFinite(kbps)) return '—'
  const mbps = kbps / 1000
  return `${mbps >= 10 ? Math.round(mbps) : mbps.toFixed(1)} Mbps`
}

/**
 * Milliseconds with one decimal ("3.8 ms").
 *
 * @param {number} ms Milliseconds.
 * @returns {string}
 */
export function formatMs(ms) {
  return Number.isFinite(ms) ? `${ms.toFixed(1)} ms` : '—'
}

/**
 * A stream's elapsed time as a clock ("00:42:13").
 *
 * @param {number} startedAt Unix seconds.
 * @param {number} [nowMs] Current time in ms.
 * @returns {string}
 */
export function formatClock(startedAt, nowMs = Date.now()) {
  if (!startedAt) return '00:00:00'
  const total = Math.max(0, Math.floor(nowMs / 1000 - startedAt))
  const pad = (n) => String(n).padStart(2, '0')
  return `${pad(Math.floor(total / 3600))}:${pad(Math.floor(total / 60) % 60)}:${pad(total % 60)}`
}

/**
 * A duration in the compact style of the design ("48 min", "1 h 12", "< 1 min").
 *
 * @param {number} seconds Duration.
 * @returns {string}
 */
export function formatDuration(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return '—'
  if (seconds < 60) return '< 1 min'
  const minutes = Math.floor(seconds / 60)
  if (minutes < 60) return `${minutes} min`
  return `${Math.floor(minutes / 60)} h ${String(minutes % 60).padStart(2, '0')}`
}

/**
 * Width percentages for the capture/encode/send latency bar. Segments under 4 % are widened
 * to 4 % so each stays visible; the rest shrink to keep the total at 100.
 *
 * @param {{capture?: number, encode?: number, send?: number}|null} latency Per-stage milliseconds.
 * @returns {{capture: number, encode: number, send: number}|null} Percentages, or null without data.
 */
export function latencySplit(latency) {
  if (!latency) return null
  const parts = ['capture', 'encode', 'send'].map((k) => (Number.isFinite(latency[k]) && latency[k] > 0 ? latency[k] : 0))
  const total = parts.reduce((a, b) => a + b, 0)
  if (total <= 0) return null
  const raw = parts.map((p) => (p / total) * 100)
  const floored = raw.map((p) => (p > 0 ? Math.max(p, 4) : 0))
  const scale = 100 / floored.reduce((a, b) => a + b, 0)
  const [capture, encode, send] = floored.map((p) => Math.round(p * scale * 10) / 10)
  return { capture, encode, send }
}

/**
 * What kind of entry an app is, for the Library card ("game" | "app" | "desktop").
 *
 * @param {object} app App from /api/apps.
 * @returns {'game'|'app'|'desktop'}
 */
export function appKind(app) {
  if (!app) return 'app'
  if (app['nova-source'] || /lutris|steam|heroic|umu-run|run-windows-exe|wine/i.test(app.cmd || '')) return 'game'
  if (/desktop/i.test(app.name || '') && !app.cmd) return 'desktop'
  return 'app'
}

/**
 * Pick the display the preview should show: the configured one, else primary, else first.
 *
 * @param {object[]} displays From /api/displays.
 * @param {string} [chosen] Name picked by the user.
 * @returns {object|null}
 */
export function pickDisplay(displays, chosen) {
  if (!Array.isArray(displays) || displays.length === 0) return null
  return displays.find((d) => d.name === chosen) || displays.find((d) => d.configured) ||
    displays.find((d) => d.primary) || displays[0]
}

/**
 * "HDMI-0 · 1920×1080 · 60 Hz" for a display.
 *
 * @param {object|null} display From /api/displays.
 * @returns {string}
 */
export function displayLabel(display) {
  if (!display) return ''
  const w = display.mode_width || display.width
  const h = display.mode_height || display.height
  const parts = [display.name]
  if (w && h) parts.push(`${w}×${h}`)
  if (Number.isFinite(display.refresh_hz) && display.refresh_hz > 0) parts.push(`${Math.round(display.refresh_hz)} Hz`)
  return parts.join(' · ')
}

/**
 * Turn /api/health checks into NvAttention issues (warnings and errors only).
 *
 * @param {object[]} checks From /api/health.
 * @param {(key: string, params?: object) => string} t i18n function.
 * @returns {object[]} Issues, errors first.
 */
export function attentionFromHealth(checks, t) {
  if (!Array.isArray(checks)) return []
  return checks
    .filter((c) => c.status === 'warn' || c.status === 'warning' || c.status === 'error')
    .sort((a, b) => (a.status === 'error' ? -1 : 0) - (b.status === 'error' ? -1 : 0))
    .map((c) => {
      const fix = c.fix || null
      const issue = { id: c.id, title: c.title, detail: c.detail, severity: c.status === 'error' ? 'danger' : 'warning' }
      if (fix?.kind === 'command') issue.command = fix.value
      if (fix?.kind === 'setting') issue.action = { label: t('nova.overview.open_setting'), to: `/settings#${fix.value}` }
      if (fix?.kind === 'doc') issue.action = { label: t('nova.overview.read_more'), href: fix.value }
      return issue
    })
}

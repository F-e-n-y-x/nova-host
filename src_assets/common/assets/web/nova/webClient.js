/**
 * @file Nova's browser client: a sidecar Nova runs when it is switched on (off by default). It
 * pairs with Nova like any Moonlight client and streams to a web browser over WebRTC, using
 * moonlight-web-stream. It listens over HTTPS on the host this web UI was opened with, on its own
 * port, and signs people in with this web UI's session (see tools/nova-web-client/README.md).
 */
import { fetchJson, postJson } from './api'

/** Gateway port for Nova's default base port (47989 + 6); the real one comes from /api/web-client. */
export const WEB_CLIENT_PORT = 47995

/** Display modes Nova accepts for a launch (`nova_display`). */
export const DISPLAY_MODES = ['virtual', 'mirror']

/** Upstream project, credited wherever the browser client is offered. */
export const UPSTREAM = { name: 'moonlight-web-stream', url: 'https://github.com/MrCreativ3001/moonlight-web-stream', licence: 'GPL-3.0' }

/**
 * The browser client's status.
 *
 * @returns {Promise<{installed: boolean, enabled: boolean, state: string, port: number, allowed: string,
 *   version: string, restarts: number, last_error: string, udp_min: number, udp_max: number}>}
 *   `state` is off, not_installed, starting, running or failed; `allowed` is pc or lan.
 */
export function getWebClient() {
  return fetchJson('./api/web-client')
}

/**
 * Switch the browser client on or off (takes effect at once and is saved as `web_client`).
 *
 * @param {boolean} enabled On or off.
 * @returns {Promise<object>} The new status.
 */
export function setWebClient(enabled) {
  return postJson('./api/web-client', { enabled: Boolean(enabled) })
}

/**
 * Base URL of the browser client for the host this page was loaded from. The same host name
 * matters: the browser then sends this web UI's sign-in to the browser client too.
 *
 * @param {Location | URL} [where] Page location (defaults to window.location).
 * @param {number} [port] Gateway port.
 * @returns {string} e.g. "https://192.168.10.10:47995".
 */
export function webClientBase(where = window.location, port = WEB_CLIENT_PORT) {
  const name = where.hostname
  const host = name.includes(':') && !name.startsWith('[') ? `[${name}]` : name
  return `https://${host}:${Number(port) || WEB_CLIENT_PORT}`
}

/**
 * The display a game starts on when the choice is left to the app: its saved display mode,
 * else a virtual display (Nova's default).
 *
 * @param {object} app An apps.json entry.
 * @returns {'virtual' | 'mirror'} The default mode.
 */
export function defaultDisplayMode(app) {
  const mode = app?.['nova-display-mode']
  return DISPLAY_MODES.includes(mode) ? mode : 'virtual'
}

/**
 * URL that opens the browser client straight into a stream of one app.
 *
 * @param {string} appName App name as in apps.json (the GameStream title).
 * @param {string} mode 'virtual' or 'mirror'.
 * @param {object} [options] Options.
 * @param {Location | URL} [options.where] Page location.
 * @param {number} [options.port] Gateway port.
 * @returns {string} The /nova/play URL.
 */
export function playInBrowserUrl(appName, mode, { where = window.location, port = WEB_CLIENT_PORT } = {}) {
  const query = new URLSearchParams({ app: appName })
  if (DISPLAY_MODES.includes(mode)) query.set('display', mode)
  return `${webClientBase(where, port)}/nova/play?${query}`
}

/**
 * Where to send the browser back to after signing in (the `continue` query the browser client
 * puts on its sign-in link). Only paths on the browser client itself are accepted.
 *
 * @param {unknown} value The query value.
 * @returns {string} A safe path, or '' when there is nothing (valid) to continue to.
 */
export function continuePath(value) {
  const path = typeof value === 'string' ? value : ''
  if (!path.startsWith('/') || path.startsWith('//') || path.includes('\\') || path.length > 2048) return ''
  // eslint-disable-next-line no-control-regex
  if (/[\u0000- \u007f]/.test(path)) return ''
  return path
}

/**
 * Whether this browser can reach the browser client. A failure also covers a certificate the
 * browser doesn't trust yet; opening the link lets the user accept it.
 *
 * @param {object} [options] Options.
 * @param {number} [options.timeoutMs] Give up after this long (default 2500).
 * @param {typeof fetch} [options.fetchImpl] fetch to use (tests).
 * @param {Location | URL} [options.where] Page location.
 * @param {number} [options.port] Gateway port.
 * @returns {Promise<boolean>} True when /nova/health answered.
 */
export async function probeWebClient({ timeoutMs = 2500, fetchImpl = fetch, where = window.location, port = WEB_CLIENT_PORT } = {}) {
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), timeoutMs)
  try {
    const response = await fetchImpl(`${webClientBase(where, port)}/nova/health`, {
      cache: 'no-store', credentials: 'omit', signal: controller.signal,
    })
    return response.ok
  } catch {
    return false
  } finally {
    clearTimeout(timer)
  }
}

import { notifyKey } from './Notification.vue'
import { csrfToken, getSession, redirectToLogin } from './nova/auth'

/**
 * The set of error messages that indicate a CSRF validation failure.
 */
const CSRF_ERRORS = new Set(['Missing CSRF token', 'Invalid CSRF token', 'CSRF token expired'])

const BUILD_HEADER = 'X-Nova-Build'
const RELOAD_KEY = 'nova-build-reloaded'
let pageBuild = null

/**
 * Remember the Web UI build this page was loaded with and reload once when the server
 * reports another one. A tab left open across a Nova upgrade otherwise keeps running the old
 * code against the new API (for example, a Pair page that never shows the suggested name).
 *
 * @param {Response} response - Any API response.
 * @param {{ reload?: () => void, storage?: Storage }} [env] - Injected in tests.
 * @returns {boolean} True when a reload was started.
 */
export function checkBuild(response, env = {}) {
  const build = response?.headers?.get?.(BUILD_HEADER)
  if (!build) return false
  if (pageBuild === null) {
    pageBuild = build
    return false
  }
  if (build === pageBuild) return false
  let storage = env.storage
  try {
    storage ??= globalThis.sessionStorage
  } catch {
    storage = undefined
  }
  try {
    // One reload per build: if the reloaded page still gets another value, don't loop.
    if (storage?.getItem(RELOAD_KEY) === build) return false
    storage?.setItem(RELOAD_KEY, build)
  } catch {
    // Storage blocked: reload anyway, a loop needs the same mismatch again.
  }
  pageBuild = build
  const reload = env.reload ?? (() => globalThis.location?.reload())
  reload()
  return true
}

/** Forget the remembered build (tests). */
export function resetBuildCheck() {
  pageBuild = null
}

const SAFE_METHODS = new Set(['GET', 'HEAD', 'OPTIONS'])

/**
 * Add the headers every API call from the page carries: X-Requested-With (so the host never
 * answers with a Basic-auth challenge, which would open the browser's sign-in popup) and, on
 * state-changing requests, this session's CSRF token.
 *
 * @param {RequestInit} [options] Fetch options.
 * @returns {RequestInit} Options with the headers added.
 */
function withAuthHeaders(options = {}) {
  const given = options.headers
  const has = (name) => {
    const lower = name.toLowerCase()
    if (!given) return false
    if (typeof given.has === 'function') return given.has(name)
    const keys = Array.isArray(given) ? given.map(([key]) => key) : Object.keys(given)
    return keys.some((key) => key.toLowerCase() === lower)
  }
  const extra = {}
  if (!has('X-Requested-With')) extra['X-Requested-With'] = 'XMLHttpRequest'
  const method = (options.method || 'GET').toUpperCase()
  if (!SAFE_METHODS.has(method) && csrfToken() && !has('X-CSRF-Token')) extra['X-CSRF-Token'] = csrfToken()
  let headers
  if (given && typeof given.set === 'function') {
    headers = new Headers(given)
    for (const [key, value] of Object.entries(extra)) headers.set(key, value)
  } else if (Array.isArray(given)) {
    headers = [...given, ...Object.entries(extra)]
  } else {
    headers = { ...given, ...extra }
  }
  return { credentials: 'same-origin', ...options, headers }
}

async function csrfError(response) {
  if (response.status !== 400) return false
  try {
    const body = await response.clone().json()
    return Boolean(body && CSRF_ERRORS.has(body.error))
  } catch (e) {
    console.debug('apiFetch: response body is not JSON', e)
    return false
  }
}

/**
 * Wrapper around the native fetch for the host API.
 *
 * - Adds X-Requested-With and, for state-changing requests, the session's CSRF token.
 * - 401 (signed out, or the session expired): goes to /login, returning here afterwards.
 * - A CSRF rejection: fetches the session's current token and retries once, then shows a notice.
 *
 * @param {string} url - The URL to fetch.
 * @param {RequestInit} [options] - Standard fetch options.
 * @returns {Promise<Response>} The fetch Response.
 */
export async function apiFetch(url, options) {
  let response = await fetch(url, withAuthHeaders(options))
  checkBuild(response)

  if (response.status === 401) {
    redirectToLogin()
    return response
  }

  if (await csrfError(response)) {
    const retryable = typeof options?.body === 'string' || options?.body == null
    let retried = false
    if (retryable) {
      try {
        const before = csrfToken()
        await getSession()
        if (csrfToken() && csrfToken() !== before) {
          response = await fetch(url, withAuthHeaders(options))
          retried = true
        }
      } catch (e) {
        console.debug('apiFetch: could not refresh the CSRF token', e)
      }
    }
    if (!retried || (await csrfError(response))) {
      notifyKey.error('_common.csrf_error_desc', '_common.csrf_error')
    }
  }

  return response
}

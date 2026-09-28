import { notifyKey } from './Notification.vue'

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

/**
 * Wrapper around the native fetch that automatically detects CSRF errors
 * (HTTP 400 with a known CSRF error message) and displays a notification.
 *
 * @param {string} url - The URL to fetch.
 * @param {RequestInit} [options] - Standard fetch options.
 * @returns {Promise<Response>} The fetch Response.
 */
export async function apiFetch(url, options) {
  const response = await fetch(url, options)
  checkBuild(response)

  if (response.status === 400) {
    let body = null
    try {
      body = await response.clone().json()
    } catch (e) {
      console.debug('apiFetch: response body is not JSON', e)
    }

    if (body && CSRF_ERRORS.has(body.error)) {
      notifyKey.error('_common.csrf_error_desc', '_common.csrf_error')
    }
  }

  return response
}

/**
 * @file Small data layer for Nova pages: JSON fetch with errors, a shared /api/config
 * cache, and a composable for loading/error state.
 */
import { ref } from 'vue'
import { apiFetch } from '../fetch_utils'
import { signOut } from './auth'

/**
 * Fetch JSON from the host API.
 *
 * @param {string} url Relative API URL, e.g. './api/apps'.
 * @param {RequestInit} [options] Fetch options.
 * @returns {Promise<any>} Parsed JSON body.
 * @throws {Error} With `status` set when the response is not OK.
 */
export async function fetchJson(url, options) {
  const response = await apiFetch(url, options)
  if (!response.ok) {
    const error = new Error(`${response.status} ${response.statusText}`.trim())
    error.status = response.status
    throw error
  }
  return response.json()
}

/**
 * POST JSON to the host API (same-origin, so no CSRF token is needed).
 *
 * @param {string} url Relative API URL.
 * @param {object} [body] JSON body.
 * @returns {Promise<any>} Parsed JSON body.
 */
export function postJson(url, body = {}) {
  return fetchJson(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
}

let configPromise = null

/**
 * The host configuration (/api/config), fetched once and shared.
 *
 * @param {boolean} [refresh] Fetch again instead of using the cache.
 * @returns {Promise<object>} The configuration object.
 */
export function getConfig(refresh = false) {
  if (refresh || !configPromise) {
    configPromise = fetchJson('./api/config').catch((error) => {
      configPromise = null
      throw error
    })
  }
  return configPromise
}

/**
 * Track an async load with `data`, `error` and `loading` refs.
 *
 * @param {() => Promise<any>} loader Function that returns the data.
 * @param {object} [options] Options.
 * @param {boolean} [options.immediate] Start loading right away (default true).
 * @returns {{data: import('vue').Ref, error: import('vue').Ref, loading: import('vue').Ref, reload: () => Promise<void>}}
 */
export function useAsync(loader, { immediate = true } = {}) {
  const data = ref(null)
  const error = ref(null)
  const loading = ref(false)

  async function reload() {
    loading.value = true
    error.value = null
    try {
      data.value = await loader()
    } catch (e) {
      error.value = e
    } finally {
      loading.value = false
    }
  }

  if (immediate) {
    loading.value = true
    reload()
  }
  return { data, error, loading, reload }
}

/**
 * Sign out: the host forgets this browser's session, then the sign-in page is shown.
 *
 * @returns {Promise<void>}
 */
export function logout() {
  return signOut()
}

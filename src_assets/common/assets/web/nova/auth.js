/**
 * @file Web UI sign-in: the session cookie is HttpOnly, so the page only knows what
 * /api/auth/session tells it. Keeps this session's CSRF token for state-changing requests,
 * sends signed-out pages to /login (keeping where they were), and signs out.
 */

/** Paths that must never be a post-sign-in destination (they would loop or sign out again). */
const NO_RETURN = new Set(['/login', '/login/', '/logout', '/logout/', '/welcome', '/welcome/'])

let csrf = ''
let redirecting = false

/** Page navigation, replaceable in tests (jsdom's location can't be stubbed). */
export const nav = {
  assign: (url) => globalThis.location.assign(url),
  replace: (url) => globalThis.location.replace(url),
}

/**
 * A post-sign-in destination, if it is a same-origin relative path.
 * Mirrors web_session::is_safe_next() on the host.
 *
 * @param {unknown} value Candidate, e.g. the `next` query parameter (already decoded).
 * @returns {string|null} The path, or null when it is missing or unsafe.
 */
export function safeNext(value) {
  if (typeof value !== 'string' || value.length === 0 || value.length > 2048) return null
  if (value[0] !== '/') return null
  // "//host" and "/\host" are scheme-relative: browsers treat them as another site.
  if (value.length > 1 && (value[1] === '/' || value[1] === '\\')) return null
  // eslint-disable-next-line no-control-regex
  if (/[\u0000-\u001f\u007f\\]/.test(value)) return null
  const path = value.split(/[?#]/, 1)[0]
  if (NO_RETURN.has(path)) return null
  // Final check: resolving it must stay on this origin.
  try {
    const base = globalThis.location?.origin || 'https://nova.invalid'
    if (new URL(value, base).origin !== new URL(base).origin) return null
  } catch {
    return null
  }
  return value
}

/**
 * The sign-in URL that returns to @p target afterwards.
 *
 * @param {string} [target] Path with query and hash; defaults to the current location.
 * @returns {string} e.g. "/login?next=%2Fsettings".
 */
export function loginUrl(target) {
  const loc = globalThis.location
  const here = target ?? (loc ? `${loc.pathname}${loc.search}${loc.hash}` : '/')
  const next = safeNext(here)
  return next && next !== '/' ? `/login?next=${encodeURIComponent(next)}` : '/login'
}

/** @returns {string} This session's CSRF token ('' until known). */
export function csrfToken() {
  return csrf
}

/** @param {string} token Remember this session's CSRF token (from sign-in or /api/auth/session). */
export function setCsrfToken(token) {
  csrf = typeof token === 'string' ? token : ''
}

/**
 * Ask the host whether this browser is signed in.
 *
 * @param {typeof fetch} [fetchImpl] Injected in tests.
 * @returns {Promise<{authenticated: boolean, setup_required: boolean, username?: string, csrf_token?: string, expires?: number, remember?: boolean}>}
 */
export async function getSession(fetchImpl = globalThis.fetch) {
  const response = await fetchImpl('/api/auth/session', { cache: 'no-store', credentials: 'same-origin', headers: { 'X-Requested-With': 'XMLHttpRequest' } })
  if (!response.ok) throw new Error(`${response.status}`)
  const body = await response.json()
  if (body?.csrf_token) setCsrfToken(body.csrf_token)
  return body
}

/**
 * Sign in.
 *
 * @param {{username: string, password: string, remember: boolean}} credentials Form values.
 * @param {typeof fetch} [fetchImpl] Injected in tests.
 * @returns {Promise<{ok: true} | {ok: false, reason: 'wrong'|'locked'|'setup'|'error', retryAfter?: number, message?: string}>}
 */
export async function signIn({ username, password, remember }, fetchImpl = globalThis.fetch) {
  let response
  try {
    response = await fetchImpl('/api/auth/login', {
      method: 'POST',
      credentials: 'same-origin',
      cache: 'no-store',
      headers: { 'Content-Type': 'application/json', 'X-Requested-With': 'XMLHttpRequest' },
      body: JSON.stringify({ username, password, remember: Boolean(remember) }),
    })
  } catch (error) {
    return { ok: false, reason: 'error', message: error?.message || '' }
  }
  let body = null
  try {
    body = await response.json()
  } catch {
    body = null
  }
  if (response.ok && body?.status === true) {
    setCsrfToken(body.csrf_token)
    return { ok: true }
  }
  if (response.status === 429) {
    const header = Number.parseInt(response.headers?.get?.('Retry-After') ?? '', 10)
    const retryAfter = Number.isFinite(header) ? header : Number(body?.retry_after) || 30
    return { ok: false, reason: 'locked', retryAfter: Math.max(1, retryAfter) }
  }
  if (response.status === 401) return { ok: false, reason: 'wrong' }
  if (response.status === 409 && body?.error === 'setup_required') return { ok: false, reason: 'setup' }
  return { ok: false, reason: 'error', message: body?.error || `${response.status}` }
}

/**
 * Send this page to the sign-in page, keeping where it was. Called when the host answers 401.
 * Does nothing on the sign-in and setup pages themselves, and only once per page.
 *
 * @returns {boolean} True when a redirect was started.
 */
export function redirectToLogin() {
  const path = globalThis.location?.pathname || '/'
  if (redirecting || NO_RETURN.has(path)) return false
  redirecting = true
  nav.assign(loginUrl())
  return true
}

/** Forget module state (tests). */
export function resetAuthState() {
  csrf = ''
  redirecting = false
}

/**
 * Sign out on the host (deletes the session and its cookie), then show the sign-in page.
 *
 * @returns {Promise<void>}
 */
export async function signOut() {
  try {
    await globalThis.fetch('/api/auth/logout', {
      method: 'POST',
      credentials: 'same-origin',
      cache: 'no-store',
      headers: { 'X-Requested-With': 'XMLHttpRequest', ...(csrf ? { 'X-CSRF-Token': csrf } : {}) },
    })
  } catch {
    // Unreachable host: the cookie stays until it expires, but the page still leaves.
  }
  setCsrfToken('')
  nav.replace('/login?signed_out=1')
}

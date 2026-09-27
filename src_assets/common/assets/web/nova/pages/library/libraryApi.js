/**
 * @file Library helpers: the host's game-library API (scan / import jobs, artwork search),
 * and pure functions that classify apps for the Library page.
 *
 * Host contract (nova-host library routes):
 *   POST /api/library/scan {source, path?}            → {job_id}
 *   POST /api/library/import {items:[…]}              → {job_id}
 *   GET  /api/library/jobs/<id>                        → {state: running|done|failed, stage,
 *                                                         progress:{done,total}, result?, error?}
 *   GET  /api/library/artwork/search?q=|appid=         → {matches:[{appid,name,confidence}], artwork:{poster|hero|logo|icon:[…]}}
 *   GET  /api/library/candidates/<id>                  → preview image
 *   GET  /api/covers/<index>/<poster|hero|logo|icon>   → stored artwork
 */
import { apiFetch } from '../../../fetch_utils'
import { fetchJson, postJson } from '../../api'

/** Scan sources, in the order the Add games sheet lists them. */
export const SOURCES = ['folder', 'lutris', 'steam', 'heroic']
/** Artwork kinds, in picker tab order. */
export const ART_KINDS = ['poster', 'hero', 'logo', 'icon']
/** Confidence at or above which a match needs no review. */
export const SURE_MATCH = 0.85
/** How often a running job is polled. */
export const POLL_MS = 700

/**
 * Whether the host has the library API. An older host answers unknown API paths with the
 * web UI's HTML page (or a 404), so anything that isn't JSON counts as "absent".
 *
 * @param {typeof apiFetch} [fetcher] Fetch implementation (tests).
 * @returns {Promise<boolean>} True when scans and imports are available.
 */
export async function probeLibraryApi(fetcher = apiFetch) {
  try {
    const response = await fetcher('./api/library/artwork/search?q=')
    if (response.status === 404) return false
    return (response.headers.get('content-type') || '').includes('json')
  } catch {
    return false
  }
}

/**
 * Start a scan.
 *
 * @param {'folder'|'lutris'|'steam'|'heroic'} source Where to look.
 * @param {string} [path] Folder to scan (folder source only).
 * @returns {Promise<string>} The job id.
 */
export async function startScan(source, path) {
  const body = source === 'folder' ? { source, path } : { source }
  return (await postJson('./api/library/scan', body)).job_id
}

/**
 * Start importing reviewed items.
 *
 * @param {object[]} items Import items ({temp_id, title?, poster?…}).
 * @returns {Promise<string>} The job id.
 */
export async function startImport(items) {
  return (await postJson('./api/library/import', { items })).job_id
}

/**
 * Read a job's state once.
 *
 * @param {string} id Job id.
 * @returns {Promise<object>} The job.
 */
export function getJob(id) {
  return fetchJson(`./api/library/jobs/${encodeURIComponent(id)}`)
}

/**
 * Poll a job until it finishes, reporting each snapshot.
 *
 * @param {string} id Job id.
 * @param {object} [options] Options.
 * @param {(job: object) => void} [options.onUpdate] Called with every snapshot.
 * @param {AbortSignal} [options.signal] Stops polling (rejects with an AbortError).
 * @param {number} [options.interval] Poll interval in ms.
 * @param {(id: string) => Promise<object>} [options.fetchJob] Job reader (tests).
 * @returns {Promise<object>} The finished job; rejects with the job's error when it failed.
 */
export async function pollJob(id, { onUpdate, signal, interval = POLL_MS, fetchJob = getJob } = {}) {
  for (;;) {
    if (signal?.aborted) throw new DOMException('Stopped', 'AbortError')
    const job = await fetchJob(id)
    onUpdate?.(job)
    if (job.state === 'done') return job
    if (job.state === 'cancelled') throw new DOMException('Stopped', 'AbortError')
    if (job.state === 'failed') {
      const error = new Error(job.error || 'failed')
      error.job = job
      throw error
    }
    await new Promise((resolve) => setTimeout(resolve, interval))
  }
}

/**
 * Search artwork and title matches.
 *
 * @param {{q?: string, appid?: number}} query Title or Steam app id.
 * @returns {Promise<{matches: object[], artwork: object}>} Results with empty defaults.
 */
export async function searchArtwork({ q, appid }) {
  const params = new URLSearchParams(appid ? { appid: String(appid) } : { q: q || '' })
  const body = await fetchJson(`./api/library/artwork/search?${params}`)
  return { matches: body?.matches || [], artwork: normalizeArtwork(body?.artwork) }
}

/**
 * An artwork map with every kind present.
 *
 * @param {object} [artwork] Host artwork map.
 * @returns {{poster: object[], hero: object[], logo: object[], icon: object[]}} Normalized map.
 */
export function normalizeArtwork(artwork) {
  return Object.fromEntries(ART_KINDS.map((k) => [k, Array.isArray(artwork?.[k]) ? artwork[k] : []]))
}

/**
 * Preview URL for a candidate image.
 *
 * @param {{id: string}} candidate Artwork candidate.
 * @returns {string} Image URL.
 */
export function candidateUrl(candidate) {
  return candidate?.id ? `./api/library/candidates/${encodeURIComponent(candidate.id)}` : ''
}

/**
 * Progress as a 0–1 fraction, or null when the total isn't known yet.
 *
 * @param {object} job Job snapshot.
 * @returns {number|null} Fraction done.
 */
export function jobFraction(job) {
  const p = job?.progress
  if (!p || !p.total) return null
  return Math.min(1, Math.max(0, p.done / p.total))
}

/**
 * How sure a title match is.
 *
 * @param {object} item Detected item.
 * @returns {'sure'|'check'|'none'} Match level.
 */
export function matchLevel(item) {
  if (!item?.matched) return 'none'
  return (item.matched.confidence ?? 0) >= SURE_MATCH ? 'sure' : 'check'
}

/**
 * The review state for scanned items: which are selected, and the artwork chosen for each.
 * Items already in the library start unselected; everything else starts selected, with the
 * first candidate of each kind.
 *
 * @param {object[]} items Detected items.
 * @returns {Record<string, {selected: boolean, title: string, art: Record<string, string>}>} State by temp id.
 */
export function initialReview(items) {
  return Object.fromEntries((items || []).map((item) => [item.temp_id, {
    selected: !item.already_in_library,
    title: item.title,
    art: Object.fromEntries(ART_KINDS.map((k) => [k, normalizeArtwork(item.artwork)[k][0]?.id || 'none'])),
  }]))
}

/**
 * Build the import payload for the selected items.
 *
 * @param {object[]} items Detected items.
 * @param {ReturnType<typeof initialReview>} review Review state.
 * @returns {object[]} Import items.
 */
export function importPayload(items, review) {
  return (items || []).filter((item) => review[item.temp_id]?.selected).map((item) => {
    const r = review[item.temp_id]
    const out = { temp_id: item.temp_id }
    if (r.title && r.title.trim() && r.title.trim() !== item.title) out.title = r.title.trim()
    for (const k of ART_KINDS) out[k] = r.art[k] || 'none'
    return out
  })
}

/**
 * What kind of entry an app is.
 *
 * @param {object} app Application.
 * @returns {'game'|'app'|'desktop'} Kind.
 */
export function appKind(app) {
  if (app?.['nova-source'] && app['nova-source'] !== 'manual') return 'game'
  const cmd = String(app?.cmd || '')
  if (!cmd.trim()) return 'desktop'
  if (/(steam:\/\/|lutris:|heroic:\/\/|run-windows-exe|umu-run|\bwine\b|proton)/i.test(cmd)) return 'game'
  return 'app'
}

/**
 * Where an app came from ('manual' when it was added by hand).
 *
 * @param {object} app Application.
 * @returns {string} Source id.
 */
export function appSource(app) {
  return app?.['nova-source'] || 'manual'
}

/**
 * Short runner label for an app's second line ("GE-Proton", "Steam", …), or '' for none.
 *
 * @param {object} app Application.
 * @returns {string} i18n key suffix or ''.
 */
export function appRunner(app) {
  const cmd = String(app?.cmd || '')
  if (/run-windows-exe|umu-run|proton/i.test(cmd)) return 'runner_proton'
  if (/\bwine\b/i.test(cmd)) return 'runner_wine'
  if (/steam:\/\//i.test(cmd)) return 'runner_steam'
  if (/lutris:/i.test(cmd)) return 'runner_lutris'
  if (/heroic:\/\//i.test(cmd)) return 'runner_heroic'
  return ''
}

/**
 * Stored artwork URL for an app, or '' when it has none of that kind.
 *
 * @param {object} app Application.
 * @param {number} index Its index in the apps list.
 * @param {'poster'|'hero'|'logo'|'icon'} kind Artwork kind.
 * @param {number} version Cache-busting counter.
 * @returns {string} URL.
 */
export function artUrl(app, index, kind, version = 0) {
  if (kind === 'poster') return app?.['image-path'] ? `./api/covers/${index}?v=${version}` : ''
  return app?.[`nova-${kind}`] ? `./api/covers/${index}/${kind}?v=${version}` : ''
}

/**
 * Download chosen library artwork for an app that is already in the library.
 *
 * @param {number} appIndex Index of the app in /api/apps.
 * @param {Record<string, string>} choices Candidate id per kind (poster/hero/logo/icon); "none" is skipped.
 * @returns {Promise<string>} Job id; poll it with pollJob. Rejects when nothing was chosen.
 */
export async function applyArtwork(appIndex, choices) {
  const body = { app_index: appIndex }
  for (const kind of ART_KINDS) {
    if (choices?.[kind] && choices[kind] !== 'none') body[kind] = choices[kind]
  }
  if (Object.keys(body).length === 1) throw new Error('nothing chosen')
  return (await postJson('./api/library/artwork/apply', body)).job_id
}

/**
 * Ask the host to stop a job; failures are ignored (older hosts can't cancel).
 *
 * @param {string} id Job id.
 * @returns {Promise<void>}
 */
export async function cancelJob(id) {
  try {
    await postJson(`./api/library/jobs/${encodeURIComponent(id)}/cancel`, {})
  } catch {
    // Older hosts can't cancel; the scan finishes in the background and is ignored.
  }
}

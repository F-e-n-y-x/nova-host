/**
 * @file Game details (metadata) API: library-wide refresh and cache, and one app's match.
 *
 * Host contract (nova-host metadata routes):
 *   GET  /api/library/metadata/status           → {total, matched, last_refresh_at}
 *   POST /api/library/metadata/refresh {all:true}|{app_index} → {job_id}; result {refreshed, matched}
 *   POST /api/library/metadata/clear-cache {}    → {removed}
 *   GET  /api/library/metadata/search?q=         → {steam:[{appid,name,confidence}], igdb:[{id,name,year,confidence}]}
 *   GET  /api/library/metadata/<index>           → {name, match:{source,id,name,confidence}|null,
 *                                                   override:{steam_appid,igdb_id}, details:{source,fetched_at}|null}
 *   POST /api/library/metadata/<index> {steam_appid?, igdb_id?, reset?, refetch?} → same, plus job_id when refetching
 */
import { fetchJson, postJson } from '../../api'

/**
 * Library-wide details status.
 *
 * @returns {Promise<{total: number, matched: number, last_refresh_at: number|null}>} Status.
 */
export async function getMetadataStatus() {
  const body = await fetchJson('./api/library/metadata/status')
  return { total: body?.total ?? 0, matched: body?.matched ?? 0, last_refresh_at: body?.last_refresh_at ?? null }
}

/**
 * Start re-fetching details for the whole library or one app.
 *
 * @param {number} [appIndex] App index; omit for every app.
 * @returns {Promise<string>} Job id; poll it with pollJob.
 */
export async function refreshMetadata(appIndex) {
  const body = appIndex === undefined ? { all: true } : { app_index: appIndex }
  return (await postJson('./api/library/metadata/refresh', body)).job_id
}

/**
 * Delete cached details, matches and screenshots.
 *
 * @returns {Promise<number>} Number of files removed.
 */
export async function clearMetadataCache() {
  return (await postJson('./api/library/metadata/clear-cache', {})).removed ?? 0
}

/**
 * Search Steam (and IGDB when configured) for a title.
 *
 * @param {string} q Title.
 * @returns {Promise<{steam: object[], igdb: object[]}>} Candidates with empty defaults.
 */
export async function searchMetadata(q) {
  const body = await fetchJson(`./api/library/metadata/search?${new URLSearchParams({ q })}`)
  return { steam: body?.steam || [], igdb: body?.igdb || [] }
}

/**
 * One app's current match.
 *
 * @param {number} index App index.
 * @returns {Promise<object>} Match status.
 */
export function getAppMetadata(index) {
  return fetchJson(`./api/library/metadata/${index}`)
}

/**
 * Change one app's match.
 *
 * @param {number} index App index.
 * @param {{steam_appid?: number|null, igdb_id?: number|null, reset?: boolean, refetch?: boolean}} change Change.
 * @returns {Promise<object>} The new match status (with job_id when refetching).
 */
export function setAppMetadata(index, change) {
  return postJson(`./api/library/metadata/${index}`, change)
}

/**
 * Merge search results into one list, best match first.
 *
 * @param {{steam: object[], igdb: object[]}} results Search results.
 * @returns {{key: string, source: 'steam'|'igdb', id: number, name: string, year: string, confidence: number}[]} Candidates.
 */
export function matchCandidates(results) {
  const steam = (results?.steam || []).map((m) => ({
    key: `steam-${m.appid}`, source: 'steam', id: m.appid, name: m.name, year: '', confidence: m.confidence ?? 0,
  }))
  const igdb = (results?.igdb || []).map((m) => ({
    key: `igdb-${m.id}`, source: 'igdb', id: m.id, name: m.name, year: m.year || '', confidence: m.confidence ?? 0,
  }))
  return [...steam, ...igdb].sort((a, b) => b.confidence - a.confidence)
}

/**
 * The POST body that makes a candidate the app's match.
 *
 * @param {{source: string, id: number}} candidate Chosen candidate.
 * @returns {object} Change body (fetches details right away).
 */
export function matchChange(candidate) {
  return candidate.source === 'igdb'
    ? { igdb_id: candidate.id, steam_appid: null, refetch: true }
    : { steam_appid: candidate.id, igdb_id: null, refetch: true }
}

/**
 * Whether the app's match was set by hand rather than found automatically.
 *
 * @param {object} status Match status.
 * @returns {boolean} True when an override is stored.
 */
export function hasOverride(status) {
  return status?.override?.steam_appid != null || status?.override?.igdb_id != null
}

/**
 * "3 days ago"-style text for a Unix time in seconds.
 *
 * @param {number|null} seconds Unix time.
 * @param {string} locale Locale.
 * @param {number} [now] Current time in ms (tests).
 * @returns {string} Relative time, or '' when unknown.
 */
export function relativeTime(seconds, locale, now = Date.now()) {
  if (!seconds) return ''
  const diff = Math.round(seconds - now / 1000)
  const rtf = new Intl.RelativeTimeFormat(locale, { numeric: 'auto' })
  const steps = [[60, 'second'], [60, 'minute'], [24, 'hour'], [30, 'day'], [12, 'month'], [Infinity, 'year']]
  let value = diff
  for (const [size, unit] of steps) {
    if (Math.abs(value) < size) return rtf.format(value, unit)
    value = Math.round(value / size)
  }
  return ''
}

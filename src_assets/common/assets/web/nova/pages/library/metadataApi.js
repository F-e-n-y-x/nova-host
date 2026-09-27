/**
 * @file Game details (metadata) API: library-wide refresh and cache, and one app's match.
 *
 * Host contract (nova-host metadata routes):
 *   GET  /api/library/metadata/status           → {total, matched, last_refresh_at}
 *   POST /api/library/metadata/refresh {all:true}|{app_index} → {job_id}; result {refreshed, matched}
 *   POST /api/library/metadata/clear-cache {}    → {removed}
 *   GET  /api/library/metadata/search?q=         → {candidates:[{source,appid,igdb_id,name,year,edition,type,unlisted,poster,confidence}]}
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
 * Search every configured source (Steam, the Steam catalogue, SteamGridDB, IGDB) for a title.
 *
 * @param {string} q Title, Steam app id or store URL.
 * @returns {Promise<{candidates: object[], igdb: boolean, steamgriddb: boolean}>} Merged candidates, best first.
 */
export async function searchMetadata(q) {
  const body = await fetchJson(`./api/library/metadata/search?${new URLSearchParams({ q })}`)
  return { candidates: body?.candidates || [], igdb: !!body?.igdb, steamgriddb: !!body?.steamgriddb }
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
 * Candidates for display, with a stable key and the id the match is stored under.
 *
 * @param {{candidates: object[]}} results Search results.
 * @returns {{key: string, source: 'steam'|'igdb', id: number, appid: number|null, igdb_id: number|null, name: string,
 *            year: string, edition: string, type: string, unlisted: boolean, poster: string|null, confidence: number}[]} Candidates.
 */
export function matchCandidates(results) {
  return (results?.candidates || []).filter((c) => c.appid || c.igdb_id).map((c) => ({
    ...c,
    key: c.appid ? `steam-${c.appid}` : `igdb-${c.igdb_id}`,
    source: c.appid ? 'steam' : 'igdb',
    id: c.appid || c.igdb_id,
    year: c.year || '',
    edition: c.edition || '',
    confidence: c.confidence ?? 0,
  }))
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

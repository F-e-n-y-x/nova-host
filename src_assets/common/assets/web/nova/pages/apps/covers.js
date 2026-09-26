/**
 * @file Cover art search against LizardByte's GameDB mirror of IGDB (same source the
 * previous editor used), and saving a chosen cover on the host.
 */
import { postJson } from '../../api'

const GAMEDB_URL = 'https://raw.githubusercontent.com/LizardByte/GameDB/gh-pages'

/**
 * GameDB bucket for a name: its first two alphanumeric characters, lower case.
 *
 * @param {string} name Search name.
 * @returns {string} Bucket name ('@' when nothing is left).
 */
export function searchBucket(name) {
  const bucket = name.substring(0, Math.min(name.length, 2)).toLowerCase().replaceAll(/[^a-z\d]/g, '')
  return bucket || '@'
}

/**
 * Turn a GameDB game into a cover candidate.
 *
 * @param {object} game GameDB game record.
 * @returns {{name: string, key: string, url: string, saveUrl: string}|null} Candidate.
 */
export function coverFromGame(game) {
  const thumb = game?.cover?.url
  if (!thumb) return null
  const dot = thumb.lastIndexOf('.')
  const slash = thumb.lastIndexOf('/')
  if (dot < 0 || slash < 0) return null
  const slug = thumb.substring(slash + 1, dot)
  return {
    name: game.name,
    key: `igdb_${game.id}`,
    url: `https://images.igdb.com/igdb/image/upload/t_cover_big/${slug}.jpg`,
    saveUrl: `https://images.igdb.com/igdb/image/upload/t_cover_big_2x/${slug}.png`,
  }
}

/**
 * Search for covers whose game name starts with `name`.
 *
 * @param {string} name Game name.
 * @param {typeof fetch} [fetcher] Fetch implementation (for tests).
 * @returns {Promise<object[]>} Cover candidates.
 */
export async function searchCovers(name, fetcher = fetch) {
  if (!name) return []
  const searchName = name.replaceAll(/\s+/g, '.').toLowerCase()
  const response = await fetcher(`${GAMEDB_URL}/buckets/${searchBucket(name)}.json`)
  if (!response.ok) throw new Error('cover search failed')
  const maps = await response.json()
  const games = await Promise.all(Object.keys(maps)
    .filter((id) => maps[id].name.replaceAll(/\s+/g, '.').toLowerCase().startsWith(searchName))
    .map((id) => fetcher(`${GAMEDB_URL}/games/${id}.json`).then((r) => r.json()).catch(() => null)))
  return games.map(coverFromGame).filter(Boolean)
}

/**
 * Download a cover onto the host.
 *
 * @param {{key: string, saveUrl: string}} cover Chosen candidate.
 * @returns {Promise<string>} Path of the saved image on the host.
 */
export async function saveCover(cover) {
  const body = await postJson('./api/covers/upload', { key: cover.key, url: cover.saveUrl })
  return body.path
}

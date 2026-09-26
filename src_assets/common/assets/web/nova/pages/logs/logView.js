/**
 * @file Pure helpers for the log viewer: level groups, filtering, windowing and
 * warning/error navigation over entries from parseLogs().
 */

/** Level groups shown as filter chips, most severe first. */
export const LEVEL_GROUPS = [
  { id: 'error', levels: ['Fatal', 'Error', 'Critical'] },
  { id: 'warning', levels: ['Warning'] },
  { id: 'info', levels: ['Info'] },
  { id: 'debug', levels: ['Debug', 'Verbose'] },
]

const GROUP_BY_LEVEL = new Map(LEVEL_GROUPS.flatMap((g) => g.levels.map((level) => [level.toLowerCase(), g.id])))

/** Entries rendered at once by default; older ones load on request. */
export const PAGE_SIZE = 1000

/**
 * Filter-chip group for a log level.
 *
 * @param {string} level Level from the log line, e.g. "Warning".
 * @returns {'error'|'warning'|'info'|'debug'} Group id; unknown levels count as info.
 */
export function levelGroup(level) {
  return GROUP_BY_LEVEL.get(String(level || '').toLowerCase()) || 'info'
}

/**
 * Give every entry its position and level group.
 *
 * @param {{timestamp: string, level: string, message: string}[]} entries Parsed entries.
 * @returns {{index: number, group: string, timestamp: string, level: string, message: string}[]} Entries.
 */
export function annotate(entries) {
  return entries.map((entry, index) => ({ ...entry, index, group: levelGroup(entry.level) }))
}

/**
 * Count entries per level group.
 *
 * @param {ReturnType<typeof annotate>} entries Annotated entries.
 * @returns {Record<string, number>} Counts keyed by group id.
 */
export function countByGroup(entries) {
  const counts = Object.fromEntries(LEVEL_GROUPS.map((g) => [g.id, 0]))
  for (const entry of entries) counts[entry.group] += 1
  return counts
}

/**
 * Entries whose group is enabled and whose text contains the query (case-insensitive).
 *
 * @param {ReturnType<typeof annotate>} entries Annotated entries.
 * @param {{groups: Set<string>, query?: string}} filters Enabled groups and text filter.
 * @returns {ReturnType<typeof annotate>} Matching entries, in order.
 */
export function filterEntries(entries, { groups, query = '' }) {
  const needle = query.trim().toLowerCase()
  return entries.filter((entry) => groups.has(entry.group) &&
    (!needle || `${entry.level} ${entry.message}`.toLowerCase().includes(needle)))
}

/**
 * The newest `limit` entries (the log is shown oldest to newest, newest at the bottom).
 *
 * @param {Array} entries Filtered entries.
 * @param {number} limit Maximum entries to render.
 * @returns {{visible: Array, hidden: number}} Entries to render and how many older ones are hidden.
 */
export function windowEntries(entries, limit) {
  const hidden = Math.max(0, entries.length - limit)
  return { visible: hidden ? entries.slice(hidden) : entries, hidden }
}

/**
 * Entry indexes of warnings and errors, for previous/next navigation.
 *
 * @param {ReturnType<typeof annotate>} entries Filtered entries.
 * @returns {number[]} Entry `index` values in order.
 */
export function problemIndexes(entries) {
  return entries.filter((e) => e.group === 'error' || e.group === 'warning').map((e) => e.index)
}

/**
 * The problem to move to from the current one.
 *
 * @param {number[]} problems Problem entry indexes, ascending.
 * @param {number} current Currently selected entry index, or -1 for none.
 * @param {'next'|'prev'} direction Direction to move.
 * @returns {number} The target entry index, or -1 when there is none.
 */
export function stepProblem(problems, current, direction) {
  if (problems.length === 0) return -1
  if (direction === 'next') {
    if (current === -1) return problems[0]
    return problems.find((i) => i > current) ?? -1
  }
  if (current === -1) return problems[problems.length - 1]
  for (let i = problems.length - 1; i >= 0; i--) {
    if (problems[i] < current) return problems[i]
  }
  return -1
}

/**
 * Split text around case-insensitive matches of a query, for highlighting without v-html.
 *
 * @param {string} text Text to split.
 * @param {string} query Search text.
 * @returns {{text: string, match: boolean}[]} Parts in order.
 */
export function highlightParts(text, query) {
  const needle = (query || '').trim().toLowerCase()
  if (!needle) return [{ text, match: false }]
  const parts = []
  const haystack = text.toLowerCase()
  let from = 0
  let at = haystack.indexOf(needle)
  while (at !== -1) {
    if (at > from) parts.push({ text: text.slice(from, at), match: false })
    parts.push({ text: text.slice(at, at + needle.length), match: true })
    from = at + needle.length
    at = haystack.indexOf(needle, from)
  }
  if (from < text.length) parts.push({ text: text.slice(from), match: false })
  return parts
}

/**
 * Entries back to log-file text.
 *
 * @param {{timestamp: string, level: string, message: string}[]} entries Entries.
 * @returns {string} One line per entry, in the host's format.
 */
export function toText(entries) {
  return entries.map((e) => `[${e.timestamp}]: ${e.level ? `${e.level}: ` : ''}${e.message}`).join('\n')
}

/**
 * File name for a downloaded log.
 *
 * @param {Date} [now] Time to stamp.
 * @returns {string} e.g. "nova-host-log-20260927-031501.log".
 */
export function logFileName(now = new Date()) {
  const pad = (n) => String(n).padStart(2, '0')
  const stamp = `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}-` +
    `${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`
  return `nova-host-log-${stamp}.log`
}

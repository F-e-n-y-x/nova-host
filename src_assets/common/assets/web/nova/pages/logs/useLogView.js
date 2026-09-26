/**
 * @file View state for the log viewer: level and text filters, the rendered window, and
 * the selected warning/error. Everything else is derived from the raw log text.
 */
import { computed, readonly, shallowRef, watch } from 'vue'
import { parseLogs } from '../../logs'
import {
  LEVEL_GROUPS, PAGE_SIZE, annotate, countByGroup, filterEntries, highlightParts, problemIndexes, stepProblem, windowEntries,
} from './logView'

const ALL_GROUPS = LEVEL_GROUPS.map((g) => g.id)

/**
 * Log view state over a raw log text ref.
 *
 * @param {import('vue').Ref<string>} text Raw log text.
 * @returns {object} Read-only state, derived rows and actions.
 */
export function useLogView(text) {
  const groups = shallowRef(new Set(ALL_GROUPS))
  const query = shallowRef('')
  const limit = shallowRef(PAGE_SIZE)
  const selected = shallowRef(-1)

  const entries = computed(() => annotate(parseLogs(text.value)))
  const counts = computed(() => countByGroup(entries.value))
  const filtered = computed(() => filterEntries(entries.value, { groups: groups.value, query: query.value }))
  const windowed = computed(() => windowEntries(filtered.value, limit.value))
  const problems = computed(() => problemIndexes(filtered.value))
  const problemPosition = computed(() => problems.value.indexOf(selected.value) + 1)
  const filtersActive = computed(() => query.value.trim() !== '' || groups.value.size !== ALL_GROUPS.length)
  const filterKey = computed(() => `${[...groups.value].sort().join(',')}|${query.value}`)

  const chips = computed(() => LEVEL_GROUPS.map((g) => ({ id: g.id, count: counts.value[g.id], on: groups.value.has(g.id) })))

  const rows = computed(() => windowed.value.visible.map((entry) => ({
    index: entry.index,
    group: entry.group,
    level: entry.level,
    timestamp: entry.timestamp,
    time: entry.timestamp.slice(11),
    iso: entry.timestamp.replace(' ', 'T'),
    parts: highlightParts(entry.message, query.value),
    selected: entry.index === selected.value,
  })))

  watch(filterKey, () => {
    selected.value = -1
    limit.value = PAGE_SIZE
  })

  function setQuery(value) {
    query.value = value
  }

  function toggleGroup(id) {
    const next = new Set(groups.value)
    if (next.has(id)) next.delete(id)
    else next.add(id)
    groups.value = next
  }

  function clearFilters() {
    groups.value = new Set(ALL_GROUPS)
    query.value = ''
  }

  function clearSelection() {
    selected.value = -1
  }

  function showOlder() {
    limit.value += PAGE_SIZE
  }

  /**
   * Select the previous or next warning/error, widening the window when it is older.
   *
   * @param {'next'|'prev'} direction Direction.
   * @returns {number} The selected entry index, or -1 when there is none.
   */
  function step(direction) {
    const target = stepProblem(problems.value, selected.value, direction)
    if (target === -1) return -1
    const position = filtered.value.findIndex((e) => e.index === target)
    const needed = filtered.value.length - position
    if (needed > limit.value) limit.value = Math.ceil(needed / PAGE_SIZE) * PAGE_SIZE
    selected.value = target
    return target
  }

  return {
    query: readonly(query),
    selected: readonly(selected),
    entries, filtered, windowed, problems, problemPosition, filtersActive, filterKey, chips, rows,
    setQuery, toggleGroup, clearFilters, clearSelection, showOlder, step,
  }
}

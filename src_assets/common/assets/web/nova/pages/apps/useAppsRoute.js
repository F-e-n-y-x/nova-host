/**
 * @file Applications page state kept in the URL, so views can be linked and Back works:
 * `?q=` search, `?sort=asc|desc`, `?view=list` and `?edit=<index>|new` (the open editor).
 * The last chosen view is also remembered in localStorage as the default.
 */
import { computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'

const VIEW_KEY = 'nova.apps.view'

function storedView() {
  try {
    return globalThis.localStorage?.getItem(VIEW_KEY) === 'list' ? 'list' : 'grid'
  } catch {
    return 'grid'
  }
}

/**
 * Parse the `edit` query value.
 *
 * @param {unknown} value Raw query value.
 * @returns {number|null} -1 for a new app, the app index, or null when no editor is open.
 */
export function parseEdit(value) {
  if (value === 'new') return -1
  if (typeof value === 'string' && /^\d+$/.test(value)) return Number(value)
  return null
}

/**
 * URL-backed page state.
 *
 * @returns {{query: import('vue').WritableComputedRef<string>, sort: import('vue').WritableComputedRef<string>,
 *   view: import('vue').WritableComputedRef<string>, editIndex: import('vue').ComputedRef<number|null>,
 *   openEditor: (index: number) => void, closeEditor: () => void}}
 */
export function useAppsRoute() {
  const route = useRoute()
  const router = useRouter()
  let pushedEditor = false

  function setQuery(patch, { push = false } = {}) {
    const next = { ...route.query, ...patch }
    for (const key of Object.keys(next)) if (next[key] === null || next[key] === '') delete next[key]
    return (push ? router.push : router.replace)({ query: next, hash: route.hash })
  }

  const query = computed({
    get: () => (typeof route.query.q === 'string' ? route.query.q : ''),
    set: (q) => setQuery({ q: q || null }),
  })
  const sort = computed({
    get: () => (route.query.sort === 'asc' || route.query.sort === 'desc' ? route.query.sort : 'default'),
    set: (s) => setQuery({ sort: s === 'default' ? null : s }),
  })
  const view = computed({
    get: () => (route.query.view === 'list' || route.query.view === 'grid' ? route.query.view : storedView()),
    set: (v) => {
      try {
        globalThis.localStorage?.setItem(VIEW_KEY, v)
      } catch {
        // Private mode or blocked storage: the choice just isn't remembered.
      }
      setQuery({ view: v })
    },
  })
  const editIndex = computed(() => parseEdit(route.query.edit))

  function openEditor(index) {
    pushedEditor = true
    setQuery({ edit: index === -1 ? 'new' : String(index) }, { push: true })
  }

  function closeEditor() {
    if (pushedEditor && router.options.history.state?.back) {
      pushedEditor = false
      router.back()
    } else {
      setQuery({ edit: null })
    }
  }

  return { query, sort, view, editIndex, openEditor, closeEditor }
}

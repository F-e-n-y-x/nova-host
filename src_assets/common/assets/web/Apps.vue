<script setup>
/**
 * Library page (/library): every app and game the host can launch, as a table or a poster grid,
 * with kind/source filter chips, search and sort in the URL (useAppsRoute), the app editor side
 * panel (`?edit=<index>|new`) and the Add games sheet (folder scan, Lutris, Steam, Heroic).
 * Add games only shows when the host has the library API; "Add application manually" always does.
 */
import { computed, nextTick, onMounted, reactive, shallowRef, useTemplateRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { onBeforeRouteLeave, onBeforeRouteUpdate, useRoute, useRouter } from 'vue-router'
import { LayoutGrid, List, Plus, Search, SquareX } from '@lucide/vue'
import NvPage from './nova/components/NvPage.vue'
import NvButton from './nova/components/NvButton.vue'
import NvAlert from './nova/components/NvAlert.vue'
import NvArt from './nova/components/NvArt.vue'
import NvBadge from './nova/components/NvBadge.vue'
import NvTable from './nova/components/NvTable.vue'
import NvFilterChips from './nova/components/NvFilterChips.vue'
import NvEmptyState from './nova/components/NvEmptyState.vue'
import NvSkeleton from './nova/components/NvSkeleton.vue'
import NvActionMenu from './nova/components/NvActionMenu.vue'
import NvConfirmDialog from './nova/components/NvConfirmDialog.vue'
import AppEditor from './nova/pages/apps/AppEditor.vue'
import AddGamesSheet from './nova/pages/library/AddGamesSheet.vue'
import { useApps } from './nova/pages/apps/useApps'
import { parseEdit, useAppsRoute } from './nova/pages/apps/useAppsRoute'
import { visibleApps } from './nova/pages/apps/appForm'
import { appKind, appRunner, appSource, artUrl, probeLibraryApi } from './nova/pages/library/libraryApi'
import { postJson } from './nova/api'
import { toast } from './nova/toast'

/** Let the browser skip rendering off-screen items in very long lists. */
const LARGE_LIST = 100
const KINDS = ['all', 'game', 'app', 'desktop']

const { t, te } = useI18n()
const route = useRoute()
const router = useRouter()
const { apps, platform, total, running, coverVersion, refresh, removeApp, closeRunning } = useApps()
const { query, sort, view, editIndex, openEditor, closeEditor } = useAppsRoute()
const editor = useTemplateRef('editor')

const libraryApi = shallowRef(false)
const addOpen = shallowRef(false)
const removing = reactive({ open: false, index: -1, name: '', busy: false, error: '' })
const closing = reactive({ open: false, busy: false, error: '' })

onMounted(async () => {
  libraryApi.value = await probeLibraryApi()
  if (route.query.add === '1' && libraryApi.value) addOpen.value = true
})

const kind = computed({
  get: () => (KINDS.includes(route.query.kind) ? route.query.kind : 'all'),
  set: (k) => router.replace({ query: { ...route.query, kind: k === 'all' ? undefined : k } }),
})
const layout = computed({
  get: () => (view.value === 'grid' ? 'grid' : 'table'),
  set: (v) => { view.value = v === 'grid' ? 'grid' : 'list' },
})
const tableSort = computed({
  get: () => (sort.value === 'default' ? null : { key: 'name', dir: sort.value }),
  set: (s) => { sort.value = s ? s.dir : 'default' },
})

const all = computed(() => apps.data.value ?? [])
const counts = computed(() => {
  const c = { all: all.value.length, game: 0, app: 0, desktop: 0 }
  for (const app of all.value) c[appKind(app)] += 1
  return c
})
const kindOptions = computed(() => KINDS
  .filter((k) => k === 'all' || counts.value[k] > 0)
  .map((k) => ({ value: k, label: t(`nova.library.kind_${k}`), count: counts.value[k] })))
const list = computed(() => visibleApps(all.value, query.value, sort.value)
  .filter(({ app }) => kind.value === 'all' || appKind(app) === kind.value))
const rows = computed(() => list.value.map(({ app, index }) => ({
  index,
  app,
  name: app.name || t('nova.apps.unnamed'),
  kind: appKind(app),
  source: appSource(app),
  runner: appRunner(app),
  running: index === running.value,
})))
const initialLoad = computed(() => apps.loading.value && !apps.data.value)
const editingApp = computed(() => (editIndex.value >= 0 ? apps.data.value?.[editIndex.value] ?? null : null))
const editorOpen = computed(() => editIndex.value === -1 || !!editingApp.value)
const columns = computed(() => [
  { key: 'name', label: t('nova.library.col_name'), sortable: true },
  { key: 'source', label: t('nova.library.col_source'), width: '140px', hideBelow: 900 },
  { key: 'status', label: t('nova.library.col_status'), width: '140px', hideBelow: 600 },
  { key: 'actions', label: t('nova.library.col_actions'), srOnly: true, width: '64px', align: 'end' },
])
const pageMenu = computed(() => [
  { id: 'manual', label: t('nova.library.add_manual'), icon: Plus, onSelect: () => openEditor(-1) },
  { id: 'close', label: t('nova.apps.close_running_ellipsis'), icon: SquareX,
    onSelect: () => { Object.assign(closing, { open: true, error: '' }) } },
])

// A link to an app that no longer exists: say so and drop it from the URL.
watch([editIndex, () => apps.data.value], ([index, data]) => {
  if (index !== null && index >= 0 && data && !data[index]) {
    toast.danger(t('nova.apps.not_found'))
    closeEditor()
  }
})

/**
 * Keep unsaved edits when the URL would close the editor (Back, links, other pages).
 *
 * @param {import('vue-router').RouteLocationNormalized} to Target route.
 * @param {import('vue-router').RouteLocationNormalized} from Current route.
 * @returns {boolean|undefined} false to stay while the user decides.
 */
function guardEdits(to, from) {
  const leavingEditor = parseEdit(from.query.edit) !== null && (to.path !== from.path || parseEdit(to.query.edit) !== parseEdit(from.query.edit))
  if (!leavingEditor || !editor.value?.isDirty) return undefined
  editor.value.askDiscard(() => router.replace(to.fullPath))
  return false
}
onBeforeRouteUpdate(guardEdits)
onBeforeRouteLeave(guardEdits)

async function focusItem(index) {
  await nextTick()
  await nextTick()
  if (document.activeElement === document.body || !document.activeElement) {
    document.getElementById(`nv-app-${index}`)?.focus()
  }
}

function sourceLabel(source) {
  return te(`nova.library.source_${source}`) ? t(`nova.library.source_${source}`) : source
}

function art(app, index, k) {
  return artUrl(app, index, k, coverVersion.value)
}

function rowMenu(row) {
  return [
    { id: 'edit', label: t('nova.library.menu_edit'), onSelect: () => openEditor(row.index) },
    { id: 'art', label: t('nova.library.menu_artwork'), onSelect: () => editArtwork(row.index) },
    { divider: true, id: 'd' },
    { id: 'delete', label: t('nova.library.menu_delete'), danger: true, onSelect: () => askRemove(row) },
  ]
}

async function editArtwork(index) {
  openEditor(index)
  await nextTick()
  await nextTick()
  editor.value?.openArtwork()
}

async function onSaved(name) {
  artworkChanged = false
  const index = editIndex.value
  closeEditor()
  toast.success(t('nova.apps.saved', { name }))
  await refresh()
  const moved = apps.data.value?.findIndex((a) => a.name === name) ?? -1
  focusItem(moved >= 0 ? moved : index)
}

let artworkChanged = false

function onArtworkApplied() {
  artworkChanged = true
}

async function onEditorClose() {
  const index = editIndex.value
  closeEditor()
  if (artworkChanged) {
    artworkChanged = false
    await refresh()
  }
  focusItem(index)
}

function askRemove(row) {
  Object.assign(removing, { open: true, index: row.index, name: row.name, busy: false, error: '' })
}

async function confirmRemove() {
  removing.busy = true
  removing.error = ''
  const app = JSON.parse(JSON.stringify(apps.data.value?.[removing.index] ?? {}))
  const name = removing.name
  try {
    await removeApp(removing.index)
    removing.open = false
    toast.info(t('nova.apps.deleted', { name }), {
      timeout: 8000,
      action: { label: t('nova.common.undo'), onClick: () => restore(app, name) },
    })
  } catch {
    removing.error = t('nova.apps.delete_failed', { name })
  } finally {
    removing.busy = false
  }
}

async function restore(app, name) {
  try {
    await postJson('./api/apps', { ...app, index: -1 })
    await refresh()
    toast.success(t('nova.library.restored', { name }))
  } catch {
    toast.danger(t('nova.library.restore_failed', { name }))
  }
}

async function confirmClose() {
  closing.busy = true
  closing.error = ''
  try {
    await closeRunning()
    closing.open = false
    toast.success(t('nova.apps.closed'))
    refresh()
  } catch {
    closing.error = t('nova.apps.close_failed')
  } finally {
    closing.busy = false
  }
}

async function onImported(result) {
  await refresh()
  if (result.imported) toast.success(t('nova.library.imported', result.imported))
  if (result.duplicates) toast.info(t('nova.library.import_dups', result.duplicates))
  if (result.failed) toast.danger(t('nova.library.import_failed', result.failed))
}

function clearFilters() {
  const { q: _q, kind: _k, ...rest } = route.query
  router.replace({ query: rest })
}

function openAdd() {
  if (libraryApi.value) addOpen.value = true
  else openEditor(-1)
}
</script>

<template>
  <NvPage :title="t('nova.library.title')">
    <template #actions>
      <NvActionMenu :label="t('nova.apps.more')" :items="pageMenu" />
      <NvButton variant="primary" class="nv-lib-add" @click="openAdd">
        <Plus :size="18" aria-hidden="true" />{{ libraryApi ? t('nova.library.add_games') : t('nova.library.add_manual') }}
      </NvButton>
    </template>

    <div class="nv-lib-bar">
      <div class="nv-lib-view" role="group" :aria-label="t('nova.library.view')">
        <button type="button" :aria-pressed="layout === 'table'" :class="['nv-lib-view__btn', { 'nv-lib-view__btn--on': layout === 'table' }]"
                @click="layout = 'table'"><List :size="16" aria-hidden="true" />{{ t('nova.library.view_table') }}</button>
        <button type="button" :aria-pressed="layout === 'grid'" :class="['nv-lib-view__btn', { 'nv-lib-view__btn--on': layout === 'grid' }]"
                @click="layout = 'grid'"><LayoutGrid :size="16" aria-hidden="true" />{{ t('nova.library.view_grid') }}</button>
      </div>
      <NvFilterChips v-if="total" v-model="kind" :options="kindOptions" :label="t('nova.library.filter')" class="nv-lib-chips" />
        <label class="nv-lib-search">
          <Search :size="16" aria-hidden="true" class="nv-lib-search__icon" />
          <span class="nv-visually-hidden">{{ t('nova.library.search') }}</span>
          <input v-model="query" type="search" class="nv-input nv-lib-search__input" :placeholder="t('nova.library.search')"
                 spellcheck="false" autocomplete="off" />
        </label>
      <span class="nv-lib-sorted">{{ sort === 'default' ? t('nova.library.sorted_default') : t(`nova.library.sorted_${sort}`) }}</span>
    </div>

    <NvAlert v-if="apps.error.value" variant="danger" :title="t('nova.apps.load_failed')">
      {{ t('nova.apps.load_failed_desc') }}
      <template #actions><NvButton size="sm" @click="apps.reload()">{{ t('nova.common.retry') }}</NvButton></template>
    </NvAlert>

    <div v-else-if="initialLoad && layout === 'grid'" class="nv-lib-grid" aria-busy="true">
      <span class="nv-visually-hidden" role="status">{{ t('nova.common.loading') }}</span>
      <NvSkeleton v-for="n in 6" :key="n" height="260px" radius="var(--nv-radius-lg)" />
    </div>

    <NvEmptyState v-else-if="!initialLoad && total === 0" :title="t('nova.library.empty_title')" :description="t('nova.library.empty_desc')">
      <template #actions>
        <NvButton v-if="libraryApi" variant="primary" @click="addOpen = true"><Plus :size="18" aria-hidden="true" />{{ t('nova.library.add_games') }}</NvButton>
        <NvButton @click="openEditor(-1)">{{ t('nova.library.add_manual') }}</NvButton>
      </template>
    </NvEmptyState>

    <NvEmptyState v-else-if="!initialLoad && list.length === 0" compact :title="query ? t('nova.apps.no_results', { query }) : t('nova.library.no_kind')"
                  :description="t('nova.apps.no_results_desc')">
      <template #actions><NvButton @click="clearFilters">{{ t('nova.library.clear_filters') }}</NvButton></template>
    </NvEmptyState>

    <NvTable v-else-if="layout === 'table'" v-model:sort="tableSort" :columns="columns" :rows="rows" row-key="index"
             :selected-key="editIndex ?? null" :loading="initialLoad" :skeleton-rows="6" :caption="t('nova.library.title')"
             :class="{ 'nv-lib--large': rows.length > LARGE_LIST }" @row-click="(row) => openEditor(row.index)">
      <template #cell-name="{ row }">
        <div class="nv-lib-name">
          <NvArt class="nv-lib-name__icon" :title="row.name" :src="art(row.app, row.index, 'icon') || art(row.app, row.index, 'poster')"
                 kind="icon" radius="var(--nv-radius-md)" decorative />
          <div class="nv-lib-name__text">
            <button :id="`nv-app-${row.index}`" type="button" class="nv-lib-name__link" :title="row.name"
                    @click.stop="openEditor(row.index)">{{ row.name }}</button>
            <span class="nv-lib-name__sub">{{ row.runner ? t(`nova.library.${row.runner}`) : t(`nova.library.kind_one_${row.kind}`) }}</span>
          </div>
        </div>
      </template>
      <template #cell-source="{ row }">
        <NvBadge :variant="row.source === 'manual' ? 'neutral' : 'accent'">{{ sourceLabel(row.source) }}</NvBadge>
      </template>
      <template #cell-status="{ row }">
        <span v-if="row.running" class="nv-lib-running">{{ t('nova.library.running') }}</span>
        <span v-else class="nv-lib-muted">{{ t('nova.library.ready') }}</span>
      </template>
      <template #cell-actions="{ row }">
        <span class="nv-lib-rowmenu" @click.stop @keydown.stop>
          <NvActionMenu :label="t('nova.library.row_menu', { name: row.name })" :items="rowMenu(row)" />
        </span>
      </template>
    </NvTable>

    <ul v-else :class="['nv-lib-grid', { 'nv-lib--large': rows.length > LARGE_LIST }]" :aria-label="t('nova.library.title')">
      <li v-for="row in rows" :key="row.index" class="nv-lib-card">
        <button :id="`nv-app-${row.index}`" type="button" class="nv-lib-card__tile" :aria-label="t('nova.library.edit_name', { name: row.name })"
                @click="openEditor(row.index)">
          <span class="nv-lib-card__art">
            <NvArt :title="row.name" :src="art(row.app, row.index, 'poster')" kind="poster" decorative />
            <span v-if="row.running" class="nv-lib-card__live">{{ t('nova.library.running') }}</span>
          </span>
          <span class="nv-lib-card__text">
            <span class="nv-lib-card__name" :title="row.name">{{ row.name }}</span>
            <span class="nv-lib-card__sub">
              {{ sourceLabel(row.source) }}<template v-if="row.running"> · <span class="nv-lib-running">{{ t('nova.library.running') }}</span></template>
            </span>
          </span>
        </button>
        <div class="nv-lib-card__menu">
          <NvActionMenu size="sm" :label="t('nova.library.row_menu', { name: row.name })" :items="rowMenu(row)" />
        </div>
      </li>
    </ul>

    <AppEditor ref="editor" :open="editorOpen" :app="editingApp" :index="editIndex ?? -1" :platform="platform"
               :library-api="libraryApi" :cover-version="coverVersion" @saved="onSaved" @close="onEditorClose" @artwork-applied="onArtworkApplied"
               @delete="askRemove({ index: editIndex, name: editingApp?.name || t('nova.apps.unnamed') })" />
    <AddGamesSheet v-if="libraryApi" v-model:open="addOpen" @imported="onImported" />
    <NvConfirmDialog v-model:open="removing.open" :title="t('nova.apps.delete_title', { name: removing.name })"
                     :description="t('nova.apps.delete_desc', { name: removing.name })" :confirm-label="t('nova.apps.delete')"
                     :loading="removing.busy" :error="removing.error" @confirm="confirmRemove" />
    <NvConfirmDialog v-model:open="closing.open" :title="t('nova.apps.close_title')" :description="t('nova.apps.close_desc')"
                     :confirm-label="t('nova.apps.close_confirm')" :loading="closing.busy" :error="closing.error" @confirm="confirmClose" />
  </NvPage>
</template>

<style>
@layer components {
  .nv-lib-search {
    position: relative;
    display: flex;
    align-items: center;
    width: 260px;
  }

  .nv-lib-search__icon {
    position: absolute;
    left: 12px;
    color: var(--nv-text-muted);
    pointer-events: none;
  }

  .nv-lib-search__input.nv-input {
    padding-left: 36px;
  }

  .nv-lib-bar {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    flex-wrap: wrap;
  }

  .nv-lib-view {
    display: inline-flex;
    gap: 2px;
    padding: 3px;
    border: 1px solid var(--nv-border-strong);
    border-radius: var(--nv-radius-md);
  }

  .nv-lib-view__btn {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-2);
    min-height: 30px;
    padding: 0 var(--nv-space-3);
    border: 0;
    border-radius: var(--nv-radius-sm);
    background: transparent;
    color: var(--nv-text-secondary);
    font: inherit;
    font-size: var(--nv-text-sm);
    font-weight: 500;
    cursor: pointer;
  }

  .nv-lib-view__btn--on {
    background: var(--nv-accent);
    color: var(--nv-on-accent);
  }

  .nv-lib-search {
    margin-left: auto;
  }

  .nv-lib-sorted {
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  .nv-lib-name {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-width: 0;
  }

  .nv-lib-name__icon {
    flex-shrink: 0;
    width: 36px;
  }

  .nv-lib-name__text {
    display: flex;
    flex-direction: column;
    min-width: 0;
    line-height: 1.3;
  }

  .nv-lib-name__link {
    padding: 0;
    border: 0;
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    font-weight: 500;
    text-align: start;
    cursor: pointer;
    overflow-wrap: anywhere;
    display: -webkit-box;
    -webkit-line-clamp: 2;
    -webkit-box-orient: vertical;
    overflow: hidden;
  }

  .nv-lib-name__sub,
  .nv-lib-muted {
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  .nv-lib-running {
    color: var(--nv-success);
    font-size: var(--nv-text-sm);
    font-weight: 500;
  }

  .nv-lib-grid {
    list-style: none;
    margin: 0;
    padding: 0;
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(168px, 1fr));
    gap: var(--nv-space-5) var(--nv-space-4);
  }

  .nv-lib-card {
    position: relative;
    align-self: start;
    min-width: 0;
  }

  /* The whole tile (poster + name) is one button, so the focus ring wraps both. */
  .nv-lib-card__tile {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    width: 100%;
    padding: 0;
    border: 0;
    border-radius: var(--nv-radius-lg);
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }

  .nv-lib-card__tile:focus-visible {
    outline: var(--nv-focus-width) solid var(--nv-focus);
    outline-offset: 4px;
  }

  .nv-lib-card__art {
    position: relative;
    display: block;
    border-radius: var(--nv-radius-lg);
    overflow: hidden;
    transition: transform 120ms var(--nv-ease);
  }

  .nv-lib-card__tile:hover .nv-lib-card__art {
    transform: translateY(-2px);
  }

  .nv-lib-card__text {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
    /* room for the ⋯ menu on the right */
    padding-right: 36px;
  }

  .nv-lib-card__name {
    display: -webkit-box;
    -webkit-box-orient: vertical;
    -webkit-line-clamp: 2;
    line-clamp: 2;
    overflow: hidden;
    color: var(--nv-text);
    font-size: var(--nv-text-md);
    font-weight: 500;
    line-height: 1.3;
    overflow-wrap: anywhere;
  }

  .nv-lib-card__sub {
    overflow: hidden;
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-xs);
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-lib-card__sub .nv-lib-running {
    font-size: inherit;
  }

  .nv-lib-card__menu {
    position: absolute;
    right: 0;
    bottom: 0;
  }

  .nv-lib-card__live {
    position: absolute;
    top: var(--nv-space-2);
    right: var(--nv-space-2);
    padding: 2px 8px;
    border-radius: var(--nv-radius-pill);
    background: var(--nv-success);
    color: var(--nv-bg);
    font-size: var(--nv-text-xs);
    font-weight: 600;
  }

  .nv-lib--large > li,
  .nv-lib--large tbody tr {
    content-visibility: auto;
    contain-intrinsic-size: auto 310px;
  }

  .nv-lib--large tbody tr {
    contain-intrinsic-size: auto 56px;
  }

  @media (prefers-reduced-motion: reduce) {
    .nv-lib-card__art { transition: none; }
    .nv-lib-card__tile:hover .nv-lib-card__art { transform: none; }
  }

  @media (max-width: 1023px) {
    .nv-lib-search { width: 200px; }
  }

  @media (max-width: 767px) {
    .nv-lib-search,
    .nv-lib-sorted { display: none; }
    .nv-lib-view { width: 100%; }
    .nv-lib-view__btn { flex: 1; justify-content: center; min-height: 40px; }
    .nv-lib-chips { width: 100%; overflow-x: auto; }
    .nv-lib-grid {
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: var(--nv-space-3) var(--nv-space-2);
    }
    .nv-lib-card__menu { display: none; }
    .nv-lib-card__text { padding-right: 0; }
    .nv-lib-card__name { font-size: var(--nv-text-sm); }
    .nv-lib-add { min-height: 44px; }
  }
}
</style>

<script setup>
/**
 * Applications page: composes the toolbar, the app grid or list, the editor panel and the
 * confirm dialogs. Search, sort, view and the open editor live in the URL
 * (useAppsRoute); data and host calls live in useApps(). The host has no launch or
 * running-state API yet, so neither is shown.
 */
import { computed, nextTick, reactive, useTemplateRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { onBeforeRouteLeave, onBeforeRouteUpdate, useRouter } from 'vue-router'
import { Plus, SquareX } from '@lucide/vue'
import NvPage from './nova/components/NvPage.vue'
import NvButton from './nova/components/NvButton.vue'
import NvAlert from './nova/components/NvAlert.vue'
import NvEmptyState from './nova/components/NvEmptyState.vue'
import NvSkeleton from './nova/components/NvSkeleton.vue'
import AppsToolbar from './nova/pages/apps/AppsToolbar.vue'
import AppItem from './nova/pages/apps/AppItem.vue'
import AppEditor from './nova/pages/apps/AppEditor.vue'
import ActionMenu from './nova/pages/apps/ActionMenu.vue'
import ConfirmDialog from './nova/pages/apps/ConfirmDialog.vue'
import { useApps } from './nova/pages/apps/useApps'
import { parseEdit, useAppsRoute } from './nova/pages/apps/useAppsRoute'
import { visibleApps } from './nova/pages/apps/appForm'
import { toast } from './nova/toast'

/** Show search and sorting once the list is long enough to need them. */
const TOOLBAR_MIN = 8
/** Let the browser skip rendering off-screen items in very long lists. */
const LARGE_LIST = 100

const { t } = useI18n()
const router = useRouter()
const { apps, platform, total, coverUrl, markCoverBroken, refresh, removeApp, closeRunning } = useApps()
const { query, sort, view, editIndex, openEditor, closeEditor } = useAppsRoute()
const editor = useTemplateRef('editor')

const removing = reactive({ open: false, index: -1, name: '', busy: false })
const closing = reactive({ open: false, busy: false })

const list = computed(() => visibleApps(apps.data.value, query.value, sort.value))
const initialLoad = computed(() => apps.loading.value && !apps.data.value)
const showToolbar = computed(() => total.value >= TOOLBAR_MIN || !!query.value)
const editingApp = computed(() => (editIndex.value >= 0 ? apps.data.value?.[editIndex.value] ?? null : null))
const editorOpen = computed(() => editIndex.value === -1 || !!editingApp.value)
const pageMenu = computed(() => [{ id: 'close-running', label: t('nova.apps.close_running_ellipsis'), icon: SquareX }])

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
 * @returns {boolean|undefined} false to stay on the page while the user decides.
 */
function guardEdits(to, from) {
  const leavingEditor = parseEdit(from.query.edit) !== null && (to.path !== from.path || parseEdit(to.query.edit) !== parseEdit(from.query.edit))
  if (!leavingEditor || !editor.value?.isDirty) return undefined
  editor.value.askDiscard(() => router.replace(to.fullPath))
  return false
}
onBeforeRouteUpdate(guardEdits)
onBeforeRouteLeave(guardEdits)

/**
 * After the editor closes, put focus back on the app's tile or row when nothing else
 * took it (e.g. the editor was opened from a link, so there's no button to return to).
 *
 * @param {number} index Host index of the app to focus.
 */
async function focusItem(index) {
  if (editorOpen.value) {
    await new Promise((resolve) => {
      const stop = watch(editorOpen, (isOpen) => { if (!isOpen) { stop(); resolve() } })
    })
  }
  await nextTick()
  await nextTick()
  if (document.activeElement === document.body || !document.activeElement) {
    document.getElementById(`nv-app-${index}`)?.focus()
  }
}

async function onSaved(name) {
  const index = editIndex.value
  closeEditor()
  toast.success(t('nova.apps.saved', { name }))
  await refresh()
  const moved = apps.data.value?.findIndex((a) => a.name === name) ?? -1
  focusItem(moved >= 0 ? moved : index)
}

function onEditorClose() {
  const index = editIndex.value
  closeEditor()
  focusItem(index)
}

function askRemove(app, index) {
  Object.assign(removing, { open: true, index, name: app.name || t('nova.apps.unnamed'), busy: false })
}

async function confirmRemove() {
  removing.busy = true
  try {
    await removeApp(removing.index)
    removing.open = false
    toast.success(t('nova.apps.deleted', { name: removing.name }))
  } catch {
    toast.danger(t('nova.apps.delete_failed', { name: removing.name }))
  } finally {
    removing.busy = false
  }
}

async function confirmClose() {
  closing.busy = true
  try {
    await closeRunning()
    closing.open = false
    toast.success(t('nova.apps.closed'))
  } catch {
    toast.danger(t('nova.apps.close_failed'))
  } finally {
    closing.busy = false
  }
}
</script>

<template>
  <NvPage :title="t('nova.apps.title')">
    <template #subtitle>{{ t('nova.apps.subtitle') }}</template>
    <template #actions>
      <NvButton variant="primary" class="nv-apps-add" @click="openEditor(-1)"><Plus :size="18" aria-hidden="true" />{{ t('nova.apps.add') }}</NvButton>
    </template>

    <div class="nv-apps-bar">
      <AppsToolbar v-if="showToolbar" v-model:query="query" v-model:sort="sort" v-model:view="view"
                   :shown="list.length" :total="total" />
      <ActionMenu :label="t('nova.apps.more')" :items="pageMenu" class="nv-apps-bar__more" @select="closing.open = true" />
    </div>

    <NvAlert v-if="apps.error.value" variant="danger" :title="t('nova.apps.load_failed')">
      {{ t('nova.apps.load_failed_desc') }}
      <template #actions><NvButton size="sm" @click="apps.reload()">{{ t('nova.common.retry') }}</NvButton></template>
    </NvAlert>

    <div v-else-if="initialLoad" class="nv-apps-grid" aria-busy="true">
      <span class="nv-visually-hidden" role="status">{{ t('nova.common.loading') }}</span>
      <NvSkeleton v-for="n in 6" :key="n" height="280px" radius="var(--nv-radius-lg)" />
    </div>

    <NvEmptyState v-else-if="total === 0" :title="t('nova.apps.empty_title')" :description="t('nova.apps.empty_desc')">
      <template #actions><NvButton variant="primary" @click="openEditor(-1)"><Plus :size="18" aria-hidden="true" />{{ t('nova.apps.add') }}</NvButton></template>
    </NvEmptyState>

    <NvEmptyState v-else-if="list.length === 0" compact :title="t('nova.apps.no_results', { query })"
                  :description="t('nova.apps.no_results_desc')">
      <template #actions><NvButton @click="query = ''">{{ t('nova.apps.clear_search') }}</NvButton></template>
    </NvEmptyState>

    <ul v-else :class="[view === 'grid' ? 'nv-apps-grid' : 'nv-apps-list', { 'nv-apps--large': list.length > LARGE_LIST }]"
        :aria-label="t('nova.apps.title')">
      <AppItem v-for="{ app, index } in list" :key="index" :app="app" :index="index" :layout="view"
               :cover-url="coverUrl(app, index)"
               @edit="openEditor(index)" @delete="askRemove(app, index)" @cover-error="markCoverBroken(index)" />
    </ul>

    <AppEditor ref="editor" :open="editorOpen" :app="editingApp" :index="editIndex ?? -1" :platform="platform"
               @saved="onSaved" @close="onEditorClose" />
    <ConfirmDialog v-model:open="removing.open" :title="t('nova.apps.delete_title', { name: removing.name })"
                   :description="t('nova.apps.delete_desc', { name: removing.name })"
                   :confirm-label="t('nova.apps.delete')" :busy="removing.busy" @confirm="confirmRemove" />
    <ConfirmDialog v-model:open="closing.open" :title="t('nova.apps.close_title')" :description="t('nova.apps.close_desc')"
                   :confirm-label="t('nova.apps.close_confirm')" :busy="closing.busy" @confirm="confirmClose" />
  </NvPage>
</template>

<style>
@layer components {
  .nv-apps-bar {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-3);
    margin-bottom: var(--nv-space-5);
  }

  .nv-apps-bar > .nv-apps-toolbar {
    flex-grow: 1;
    min-width: 0;
    margin-bottom: 0;
  }

  .nv-apps-bar__more {
    margin-left: auto;
  }

  .nv-apps-grid {
    list-style: none;
    margin: 0;
    padding: 0;
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(168px, 1fr));
    gap: var(--nv-space-5) var(--nv-space-4);
  }

  .nv-apps-list {
    list-style: none;
    margin: 0;
    padding: 0;
    border-radius: var(--nv-radius-lg);
    border: 1px solid var(--nv-border);
    background: var(--nv-surface);
  }

  .nv-apps--large > .nv-app {
    content-visibility: auto;
    contain-intrinsic-size: auto 280px;
  }

  .nv-apps-list.nv-apps--large > .nv-app {
    contain-intrinsic-size: auto 64px;
  }

  @media (max-width: 899px) {
    .nv-apps-grid {
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: var(--nv-space-4) var(--nv-space-3);
    }

    .nv-apps-add {
      min-height: 44px;
    }
  }
}
</style>

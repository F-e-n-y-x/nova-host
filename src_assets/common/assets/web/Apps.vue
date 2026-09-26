<script setup>
/**
 * Applications page: composes the toolbar, the app grid or list, the editor and the
 * confirm dialogs. State and host calls live in useApps(). The host has no launch or
 * running-state API yet, so neither is shown.
 */
import { computed, reactive, shallowRef } from 'vue'
import { useI18n } from 'vue-i18n'
import { Plus } from '@lucide/vue'
import NvPage from './nova/components/NvPage.vue'
import NvButton from './nova/components/NvButton.vue'
import NvAlert from './nova/components/NvAlert.vue'
import NvEmptyState from './nova/components/NvEmptyState.vue'
import NvSkeleton from './nova/components/NvSkeleton.vue'
import AppsToolbar from './nova/pages/apps/AppsToolbar.vue'
import AppItem from './nova/pages/apps/AppItem.vue'
import AppEditorDialog from './nova/pages/apps/AppEditorDialog.vue'
import ConfirmDialog from './nova/pages/apps/ConfirmDialog.vue'
import { useApps } from './nova/pages/apps/useApps'
import { visibleApps } from './nova/pages/apps/appForm'
import { toast } from './nova/toast'

const { t } = useI18n()
const VIEW_KEY = 'nova.apps.view'

const { apps, platform, total, coverUrl, markCoverBroken, refresh, removeApp, closeRunning } = useApps()

const query = shallowRef('')
const sort = shallowRef('default')
const storedView = shallowRef(readView())
const view = computed({ get: () => storedView.value, set: saveView })

const editor = reactive({ open: false, app: null, index: -1 })
const removing = reactive({ open: false, index: -1, name: '', busy: false })
const closing = reactive({ open: false, busy: false })

const list = computed(() => visibleApps(apps.data.value, query.value, sort.value))
const initialLoad = computed(() => apps.loading.value && !apps.data.value)

function readView() {
  try {
    return globalThis.localStorage?.getItem(VIEW_KEY) === 'list' ? 'list' : 'grid'
  } catch {
    return 'grid'
  }
}

function saveView(value) {
  storedView.value = value
  try {
    globalThis.localStorage?.setItem(VIEW_KEY, value)
  } catch {
    // Private mode or blocked storage: the choice just isn't remembered.
  }
}

function openEditor(app = null, index = -1) {
  Object.assign(editor, { open: true, app, index })
}

async function onSaved() {
  toast.success(t('nova.apps.saved'))
  await refresh()
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
    toast.danger(t('nova.apps.delete_failed'))
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
      <NvButton @click="closing.open = true">{{ t('nova.apps.close_running') }}</NvButton>
      <NvButton variant="primary" @click="openEditor()"><Plus :size="18" aria-hidden="true" />{{ t('nova.apps.add') }}</NvButton>
    </template>

    <AppsToolbar v-if="total > 0" v-model:query="query" v-model:sort="sort" v-model:view="view"
                 :shown="list.length" :total="total" />

    <NvAlert v-if="apps.error.value" variant="danger" :title="t('nova.apps.load_failed')">
      {{ t('nova.apps.load_failed_desc') }}
      <template #actions><NvButton size="sm" @click="apps.reload()">{{ t('nova.common.retry') }}</NvButton></template>
    </NvAlert>

    <div v-else-if="initialLoad" class="nv-apps-grid" aria-busy="true" :aria-label="t('nova.common.loading')">
      <NvSkeleton v-for="n in 6" :key="n" height="300px" radius="var(--nv-radius-lg)" />
    </div>

    <NvEmptyState v-else-if="total === 0" :title="t('nova.apps.empty_title')" :description="t('nova.apps.empty_desc')">
      <template #actions><NvButton variant="primary" @click="openEditor()"><Plus :size="18" aria-hidden="true" />{{ t('nova.apps.add') }}</NvButton></template>
    </NvEmptyState>

    <NvEmptyState v-else-if="list.length === 0" compact :title="t('nova.apps.no_results', { query })"
                  :description="t('nova.apps.no_results_desc')" />

    <ul v-else :class="view === 'grid' ? 'nv-apps-grid' : 'nv-apps-list'" :aria-label="t('nova.apps.title')">
      <AppItem v-for="{ app, index } in list" :key="index" :app="app" :layout="view" :cover-url="coverUrl(app, index)"
               @edit="openEditor(app, index)" @delete="askRemove(app, index)" @cover-error="markCoverBroken(index)" />
    </ul>

    <AppEditorDialog v-model:open="editor.open" :app="editor.app" :index="editor.index" :platform="platform" @saved="onSaved" />
    <ConfirmDialog v-model:open="removing.open" :title="t('nova.apps.delete_title')"
                   :description="t('nova.apps.delete_desc', { name: removing.name })"
                   :confirm-label="t('nova.apps.delete')" :busy="removing.busy" @confirm="confirmRemove" />
    <ConfirmDialog v-model:open="closing.open" :title="t('nova.apps.close_title')" :description="t('nova.apps.close_desc')"
                   :confirm-label="t('nova.apps.close_confirm')" :busy="closing.busy" @confirm="confirmClose" />
  </NvPage>
</template>

<style>
@layer components {
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


  @media (max-width: 600px) {
    .nv-apps-grid {
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: var(--nv-space-3);
    }
  }
}
</style>

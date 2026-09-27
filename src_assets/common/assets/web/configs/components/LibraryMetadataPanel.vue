<script setup>
/**
 * Status and actions at the top of Settings → Library & artwork: how many games have
 * details and when the library was last refreshed, "Refresh all now" (a background job
 * with progress) and "Clear cache" (after a confirmation).
 *
 * Props: dirty (the section has unsaved changes; the host still uses the saved settings).
 */
import { computed, onBeforeUnmount, reactive, shallowRef } from 'vue'
import { useI18n } from 'vue-i18n'
import { RefreshCw, Trash2 } from '@lucide/vue'
import NvButton from '../../nova/components/NvButton.vue'
import NvConfirmDialog from '../../nova/components/NvConfirmDialog.vue'
import { toast } from '../../nova/toast.js'
import { pollJob } from '../../nova/pages/library/libraryApi'
import { clearMetadataCache, getMetadataStatus, refreshMetadata, relativeTime } from '../../nova/pages/library/metadataApi'

defineProps({
  dirty: { type: Boolean, default: false },
})
const { t, locale } = useI18n()

const status = shallowRef(null)
const statusError = shallowRef(false)
const refresh = reactive({ running: false, done: 0, total: 0 })
const clear = reactive({ open: false, busy: false, error: '' })
let polling = null

const statusText = computed(() => {
  if (!status.value) return ''
  const { matched, total } = status.value
  return t('nova.settings.meta_status', { matched, total }, total)
})
const refreshedText = computed(() => {
  const when = relativeTime(status.value?.last_refresh_at, locale.value)
  return when ? t('nova.settings.meta_last_refresh', { when }) : t('nova.settings.meta_never_refreshed')
})
const progressText = computed(() => t('nova.settings.meta_refreshing', { done: refresh.done, total: refresh.total || '…' }))

async function loadStatus() {
  try {
    status.value = await getMetadataStatus()
    statusError.value = false
  } catch {
    statusError.value = true
  }
}

async function refreshAll() {
  refresh.running = true
  refresh.done = 0
  refresh.total = 0
  polling = new AbortController()
  try {
    const job = await refreshMetadata()
    const finished = await pollJob(job, {
      signal: polling.signal,
      onUpdate: (snapshot) => {
        refresh.done = snapshot.progress?.done ?? 0
        refresh.total = snapshot.progress?.total ?? 0
      },
    })
    const result = finished.result || {}
    toast.success(t('nova.settings.meta_refresh_done', { matched: result.matched ?? 0, refreshed: result.refreshed ?? 0 }))
    await loadStatus()
  } catch (error) {
    if (error?.name !== 'AbortError') toast.danger(t('nova.settings.meta_refresh_failed', { error: error?.message || '' }))
  } finally {
    refresh.running = false
    polling = null
  }
}

async function clearCache() {
  clear.busy = true
  clear.error = ''
  try {
    await clearMetadataCache()
    clear.open = false
    toast.success(t('nova.settings.meta_cleared'))
    await loadStatus()
  } catch {
    clear.error = t('nova.settings.meta_clear_failed')
  } finally {
    clear.busy = false
  }
}

onBeforeUnmount(() => polling?.abort())
loadStatus()
</script>

<template>
  <div class="nv-meta-panel">
    <div class="nv-meta-panel__status" role="status">
      <template v-if="refresh.running">
        <span class="nv-meta-panel__primary">{{ progressText }}</span>
      </template>
      <template v-else-if="status">
        <span class="nv-meta-panel__primary">{{ statusText }}</span>
        <span class="nv-meta-panel__secondary">{{ refreshedText }}</span>
      </template>
      <span v-else-if="statusError" class="nv-meta-panel__secondary">{{ t('nova.settings.meta_status_failed') }}</span>
      <span v-if="dirty" class="nv-meta-panel__secondary">{{ t('nova.settings.meta_save_first') }}</span>
    </div>
    <div class="nv-meta-panel__actions">
      <NvButton size="sm" :loading="refresh.running" @click="refreshAll">
        <RefreshCw :size="16" aria-hidden="true" />{{ t('nova.settings.meta_refresh_all') }}
      </NvButton>
      <NvButton size="sm" variant="ghost" :disabled="refresh.running" @click="clear.open = true">
        <Trash2 :size="16" aria-hidden="true" />{{ t('nova.settings.meta_clear_cache') }}
      </NvButton>
    </div>
  </div>

  <NvConfirmDialog v-model:open="clear.open" :title="t('nova.settings.meta_clear_title')"
                   :description="t('nova.settings.meta_clear_desc')" :confirm-label="t('nova.settings.meta_clear_confirm')"
                   :loading="clear.busy" :error="clear.error" @confirm="clearCache" />
</template>

<style>
@layer components {
  .nv-meta-panel {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: space-between;
    gap: var(--nv-space-3);
    padding: var(--nv-space-4) var(--nv-space-5);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-surface);
  }

  .nv-meta-panel__status {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
  }

  .nv-meta-panel__primary {
    font-weight: 500;
  }

  .nv-meta-panel__secondary {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-meta-panel__actions {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-2);
  }

  @media (max-width: 699px) {
    .nv-meta-panel {
      padding: var(--nv-space-3) var(--nv-space-4);
    }
  }
}
</style>

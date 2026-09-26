<script setup>
/**
 * Host log card: wires the live feed (useLogFeed) and view state (useLogView) to the
 * toolbar and list, and handles copy and download.
 *
 * Props: load (optional loader, used by tests), interval (poll ms).
 */
import { computed, useId } from 'vue'
import { useI18n } from 'vue-i18n'
import NvCard from '../../components/NvCard.vue'
import NvButton from '../../components/NvButton.vue'
import NvSwitch from '../../components/NvSwitch.vue'
import NvAlert from '../../components/NvAlert.vue'
import NvEmptyState from '../../components/NvEmptyState.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import LogToolbar from './LogToolbar.vue'
import LogList from './LogList.vue'
import { toast } from '../../toast'
import { PAGE_SIZE, logFileName, toText } from './logView'
import { POLL_INTERVAL_MS, useLogFeed } from './useLogFeed'
import { useLogView } from './useLogView'

const props = defineProps({
  load: { type: Function, default: undefined },
  interval: { type: Number, default: POLL_INTERVAL_MS },
})

const { t } = useI18n()
const liveLabelId = useId()
const feed = useLogFeed({ interval: props.interval, ...(props.load ? { load: props.load } : {}) })
const { text, loading, error, updatedAt, live, hidden } = feed
const view = useLogView(text)

const query = computed({ get: () => view.query.value, set: (value) => view.setQuery(value) })

const updatedLabel = computed(() => {
  if (hidden.value && live.value) return t('nova.logs.paused_hidden')
  if (!updatedAt.value) return ''
  return t('nova.logs.updated', { time: updatedAt.value.toLocaleTimeString() })
})

const state = computed(() => {
  if (!text.value && loading.value) return 'loading'
  if (!text.value && error.value) return 'error'
  if (view.entries.value.length === 0) return 'empty'
  if (view.filtered.value.length === 0) return 'no-match'
  return 'ready'
})

async function copyShown() {
  try {
    await navigator.clipboard.writeText(toText(view.filtered.value))
    toast.success(t('nova.logs.copied', { count: view.filtered.value.length }))
  } catch {
    toast.warning(t('nova.logs.copy_failed'))
  }
}

function download() {
  const url = URL.createObjectURL(new Blob([text.value], { type: 'text/plain' }))
  const link = document.createElement('a')
  link.href = url
  link.download = logFileName()
  document.body.appendChild(link)
  link.click()
  link.remove()
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}
</script>

<template>
  <NvCard :title="t('nova.logs.log_title')">
    <template #actions>
      <span class="nv-logcard__live">
        <span class="nv-logcard__updated" aria-live="polite">{{ updatedLabel }}</span>
        <span :id="liveLabelId" class="nv-logcard__live-label" :title="t('nova.logs.live_hint')">{{ t('nova.logs.live') }}</span>
        <NvSwitch v-model="live" :labelledby="liveLabelId" />
      </span>
    </template>

    <LogToolbar v-model:query="query" :chips="view.chips.value" :problem-count="view.problems.value.length"
                :problem-position="view.problemPosition.value" :show-refresh="!live" :refreshing="loading"
                :can-copy="view.filtered.value.length > 0" :can-download="Boolean(text)"
                @toggle-group="view.toggleGroup" @prev="view.step('prev')" @next="view.step('next')"
                @refresh="feed.refresh()" @copy="copyShown" @download="download" />

    <div v-if="state === 'loading'" aria-busy="true" class="nv-logcard__placeholder">
      <NvSkeleton :lines="10" height="14px" />
    </div>
    <NvAlert v-else-if="state === 'error'" variant="danger" :title="t('nova.logs.load_failed')">
      {{ error.message }}
      <template #actions><NvButton size="sm" variant="secondary" @click="feed.refresh()">{{ t('nova.common.retry') }}</NvButton></template>
    </NvAlert>
    <NvEmptyState v-else-if="state === 'empty'" :title="t('nova.logs.empty')" />
    <NvEmptyState v-else-if="state === 'no-match'" :title="t('nova.logs.no_match')">
      <template #actions><NvButton variant="secondary" @click="view.clearFilters()">{{ t('nova.logs.clear_filters') }}</NvButton></template>
    </NvEmptyState>
    <LogList v-else :rows="view.rows.value" :hidden-count="view.windowed.value.hidden" :total-count="view.filtered.value.length"
             :page-size="PAGE_SIZE" :filter-key="view.filterKey.value" :selected="view.selected.value"
             @show-older="view.showOlder()" @latest="view.clearSelection()" />

    <template v-if="view.entries.value.length" #footer>
      <span class="nv-secondary">{{ t('nova.logs.entries', { count: view.filtered.value.length }) }}</span>
    </template>
  </NvCard>
</template>

<style>
@layer components {
  .nv-logcard__live {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
  }

  .nv-logcard__updated {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-muted);
  }

  .nv-logcard__live-label {
    font-weight: 500;
  }

  .nv-logcard__placeholder {
    padding: var(--nv-space-4);
    border-radius: var(--nv-radius-md);
    border: 1px solid var(--nv-border);
  }

  @media (max-width: 899px) {
    .nv-logcard__updated {
      display: none;
    }
  }
}
</style>

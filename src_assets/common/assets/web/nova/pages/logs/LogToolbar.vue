<script setup>
/**
 * Log viewer toolbar: level chips, text filter, warning/error navigation, refresh, copy, download.
 *
 * Props: chips ({id, count, on}[]), problemCount, problemPosition (1-based, 0 = none selected),
 * showRefresh, refreshing, canCopy, canDownload. v-model:query for the text filter.
 * Emits: toggle-group(id), prev, next, refresh, copy, download.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { ChevronDown, ChevronUp, Copy, Download, RefreshCw } from '@lucide/vue'
import NvButton from '../../components/NvButton.vue'
import NvIconButton from '../../components/NvIconButton.vue'
import NvTextField from '../../components/NvTextField.vue'

const query = defineModel('query', { type: String, default: '' })
const props = defineProps({
  chips: { type: Array, required: true },
  problemCount: { type: Number, default: 0 },
  problemPosition: { type: Number, default: 0 },
  showRefresh: { type: Boolean, default: false },
  refreshing: { type: Boolean, default: false },
  canCopy: { type: Boolean, default: false },
  canDownload: { type: Boolean, default: false },
})
defineEmits(['toggle-group', 'prev', 'next', 'refresh', 'copy', 'download'])

const { t } = useI18n()

const chipItems = computed(() => props.chips.map((chip) => ({
  ...chip,
  label: t(`nova.logs.level_${chip.id}`),
  classes: ['nv-chip', `nv-chip--${chip.id}`, { 'nv-chip--on': chip.on }],
})))

const positionLabel = computed(() => {
  if (props.problemCount === 0) return t('nova.logs.no_problems')
  if (props.problemPosition > 0) return t('nova.logs.problem_position', { current: props.problemPosition, total: props.problemCount })
  return t('nova.logs.problems', { count: props.problemCount })
})
</script>

<template>
  <div class="nv-logbar">
    <div class="nv-chips" role="group" :aria-label="t('nova.logs.levels')">
      <button v-for="chip in chipItems" :key="chip.id" type="button" :class="chip.classes"
              :aria-pressed="chip.on ? 'true' : 'false'" @click="$emit('toggle-group', chip.id)">
        <span class="nv-chip__dot" aria-hidden="true"></span>
        {{ chip.label }}
        <span class="nv-chip__count">{{ chip.count }}</span>
      </button>
    </div>
    <div class="nv-logbar__search">
      <NvTextField v-model="query" type="search" hide-label :label="t('nova.logs.search')"
                   :placeholder="t('nova.logs.search_placeholder')" autocomplete="off" />
    </div>
    <div class="nv-logbar__tools">
      <span class="nv-logbar__position nv-secondary" aria-live="polite">{{ positionLabel }}</span>
      <NvIconButton :label="t('nova.logs.prev_problem')" variant="secondary" :disabled="problemCount === 0" @click="$emit('prev')">
        <ChevronUp :size="18" aria-hidden="true" />
      </NvIconButton>
      <NvIconButton :label="t('nova.logs.next_problem')" variant="secondary" :disabled="problemCount === 0" @click="$emit('next')">
        <ChevronDown :size="18" aria-hidden="true" />
      </NvIconButton>
      <NvButton v-if="showRefresh" variant="secondary" :loading="refreshing" @click="$emit('refresh')">
        <RefreshCw :size="16" aria-hidden="true" />{{ t('nova.logs.refresh') }}
      </NvButton>
      <NvButton variant="secondary" :disabled="!canCopy" @click="$emit('copy')">
        <Copy :size="16" aria-hidden="true" />{{ t('nova.logs.copy') }}
      </NvButton>
      <NvButton variant="secondary" :disabled="!canDownload" @click="$emit('download')">
        <Download :size="16" aria-hidden="true" />{{ t('nova.logs.download') }}
      </NvButton>
    </div>
  </div>
</template>

<style>
@layer components {
  .nv-logbar {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: var(--nv-space-3);
  }

  .nv-logbar__search {
    flex: 1 1 220px;
    min-width: 0;
  }

  .nv-logbar__tools {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: var(--nv-space-2);
  }

  .nv-logbar__position {
    font-size: var(--nv-text-sm);
    margin-right: var(--nv-space-1);
  }

  .nv-chips {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-2);
  }

  .nv-chip {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-2);
    min-height: var(--nv-control-height-sm);
    padding: 0 var(--nv-space-3);
    border-radius: 18px;
    border: 1px solid var(--nv-border-strong);
    background: transparent;
    color: var(--nv-text-secondary);
    font: inherit;
    font-size: var(--nv-text-sm);
    font-weight: 500;
    cursor: pointer;
  }

  .nv-chip--on {
    background: var(--nv-accent-tint);
    border-color: var(--nv-accent-text);
    color: var(--nv-text);
  }

  .nv-chip__dot {
    width: 8px;
    height: 8px;
    border-radius: 4px;
    background: var(--nv-text-muted);
  }

  .nv-chip--error .nv-chip__dot { background: var(--nv-danger); }
  .nv-chip--warning .nv-chip__dot { background: var(--nv-warning); }
  .nv-chip--info .nv-chip__dot { background: var(--nv-text-secondary); }

  .nv-chip:not(.nv-chip--on) .nv-chip__dot {
    background: transparent;
    box-shadow: inset 0 0 0 1px var(--nv-text-muted);
  }

  .nv-chip__count {
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
  }

  @media (max-width: 899px) {
    .nv-logbar__tools {
      width: 100%;
    }

    .nv-logbar__position {
      flex-basis: 100%;
    }
  }
}
</style>

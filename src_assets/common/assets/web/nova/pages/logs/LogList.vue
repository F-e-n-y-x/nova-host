<script setup>
/**
 * Scrollable log entries. Follows the tail while the reader is at the bottom, jumps to the
 * bottom when the filters change, and scrolls the selected entry into view.
 *
 * Props: rows (from useLogView), hiddenCount, totalCount, pageSize, filterKey, selected.
 * Emits: show-older, latest (reader jumped back to the newest entries).
 */
import { computed, nextTick, onMounted, shallowRef, useTemplateRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { ArrowDownToLine } from '@lucide/vue'
import NvButton from '../../components/NvButton.vue'

const props = defineProps({
  rows: { type: Array, required: true },
  hiddenCount: { type: Number, default: 0 },
  totalCount: { type: Number, default: 0 },
  pageSize: { type: Number, required: true },
  filterKey: { type: String, default: '' },
  selected: { type: Number, default: -1 },
})
const emit = defineEmits(['show-older', 'latest'])

const { t } = useI18n()
const olderCount = computed(() => Math.min(props.hiddenCount, props.pageSize))
const scroller = useTemplateRef('scroller')
const atBottom = shallowRef(true)
let keepFromBottom = null

function onScroll() {
  const el = scroller.value
  if (el) atBottom.value = el.scrollHeight - el.scrollTop - el.clientHeight < 32
}

async function scrollToBottom() {
  await nextTick()
  const el = scroller.value
  if (!el) return
  el.scrollTop = el.scrollHeight
  atBottom.value = true
}

function showOlder() {
  const el = scroller.value
  keepFromBottom = el ? el.scrollHeight - el.scrollTop : null
  emit('show-older')
}

function jumpToLatest() {
  emit('latest')
  scrollToBottom()
}

watch(() => props.rows, async () => {
  if (keepFromBottom !== null) {
    const offset = keepFromBottom
    keepFromBottom = null
    await nextTick()
    if (scroller.value) scroller.value.scrollTop = scroller.value.scrollHeight - offset
    return
  }
  if (atBottom.value && props.selected === -1) scrollToBottom()
})

watch(() => props.filterKey, scrollToBottom)

watch(() => props.selected, async (index) => {
  if (index === -1) return
  await nextTick()
  const el = scroller.value
  const row = el?.querySelector(`[data-index="${index}"]`)
  if (!el || !row) return
  el.scrollTop = row.offsetTop - el.clientHeight / 3
  onScroll()
})

onMounted(scrollToBottom)
</script>

<template>
  <div class="nv-log-wrap">
    <div ref="scroller" class="nv-log" role="log" aria-live="off" tabindex="0"
         :aria-label="t('nova.logs.log_title')" @scroll.passive="onScroll">
      <div v-if="hiddenCount > 0" class="nv-log__older">
        <span class="nv-secondary">{{ t('nova.logs.showing', { shown: rows.length, total: totalCount }) }}</span>
        <NvButton size="sm" variant="secondary" @click="showOlder">
          {{ t('nova.logs.show_older', { count: olderCount }, olderCount) }}
        </NvButton>
      </div>
      <ol class="nv-log__list">
        <li v-for="row in rows" :key="row.index" :data-index="row.index"
            :class="['nv-log__row', `nv-log__row--${row.group}`, { 'nv-log__row--selected': row.selected }]"
            :aria-current="row.selected ? 'true' : null">
          <time class="nv-log__time" :datetime="row.iso" :title="row.timestamp">{{ row.time }}</time>
          <span class="nv-log__level">{{ row.level || t('nova.logs.level_info') }}</span>
          <span class="nv-log__msg"><template v-for="(part, i) in row.parts" :key="i"><mark v-if="part.match" class="nv-log__mark">{{ part.text }}</mark><template v-else>{{ part.text }}</template></template></span>
        </li>
      </ol>
    </div>
    <NvButton v-if="!atBottom" size="sm" variant="primary" class="nv-log__latest" @click="jumpToLatest">
      <ArrowDownToLine :size="16" aria-hidden="true" />{{ t('nova.logs.latest') }}
    </NvButton>
  </div>
</template>

<style>
@layer components {
  .nv-log-wrap {
    position: relative;
  }

  .nv-log {
    height: clamp(360px, 62vh, 760px);
    overflow: auto;
    border-radius: var(--nv-radius-md);
    border: 1px solid var(--nv-border);
    background: var(--nv-bg);
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-sm);
    line-height: 1.55;
  }

  .nv-log__older {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: space-between;
    gap: var(--nv-space-2);
    padding: var(--nv-space-2) var(--nv-space-3);
    border-bottom: 1px solid var(--nv-border);
    font-family: var(--nv-font-sans);
  }

  .nv-log__list {
    list-style: none;
    margin: 0;
    padding: var(--nv-space-2) 0;
  }

  .nv-log__row {
    display: grid;
    grid-template-columns: 96px 64px minmax(0, 1fr);
    gap: var(--nv-space-3);
    padding: 1px var(--nv-space-3);
    border-left: 3px solid transparent;
  }

  .nv-log__row--selected {
    background: var(--nv-accent-tint);
    border-left-color: var(--nv-accent-text);
  }

  .nv-log__time {
    color: var(--nv-text-muted);
    white-space: nowrap;
    font-variant-numeric: tabular-nums;
  }

  .nv-log__level {
    color: var(--nv-text-secondary);
    white-space: nowrap;
  }

  .nv-log__row--error .nv-log__level { color: var(--nv-danger); font-weight: 500; }
  .nv-log__row--warning .nv-log__level { color: var(--nv-warning); font-weight: 500; }
  .nv-log__row--debug .nv-log__level,
  .nv-log__row--debug .nv-log__msg { color: var(--nv-text-muted); }

  .nv-log__msg {
    white-space: pre-wrap;
    overflow-wrap: anywhere;
    color: var(--nv-text);
  }

  .nv-log__mark {
    background: var(--nv-warning-tint);
    color: var(--nv-text);
    border-radius: 2px;
    box-shadow: 0 0 0 1px var(--nv-warning);
  }

  .nv-log__latest {
    position: absolute;
    right: var(--nv-space-4);
    bottom: var(--nv-space-4);
    box-shadow: var(--nv-shadow);
  }

  @media (max-width: 899px) {
    .nv-log__row {
      grid-template-columns: auto minmax(0, 1fr);
      padding: var(--nv-space-1) var(--nv-space-3);
      border-bottom: 1px solid var(--nv-border);
    }

    .nv-log__msg {
      grid-column: 1 / -1;
    }
  }
}
</style>

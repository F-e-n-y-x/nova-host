<script setup>
/**
 * Dialog that browses the host's file system through /api/browse and returns a path.
 *
 * v-model:open — boolean.
 * Props: title, type ('file' | 'directory'), start (path to open at).
 * Emits: select(path).
 */
import { computed, shallowRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { ArrowUp, File, Folder } from '@lucide/vue'
import NvAlert from '../../nova/components/NvAlert.vue'
import NvButton from '../../nova/components/NvButton.vue'
import NvDialog from '../../nova/components/NvDialog.vue'
import NvSkeleton from '../../nova/components/NvSkeleton.vue'
import { fetchJson } from '../../nova/api.js'

const open = defineModel('open', { type: Boolean, default: false })
const props = defineProps({
  title: { type: String, required: true },
  type: { type: String, default: 'file' },
  start: { type: String, default: '' },
})
const emit = defineEmits(['select'])
const { t } = useI18n()

const current = shallowRef('')
const parent = shallowRef('')
const entries = shallowRef([])
const selected = shallowRef('')
const loading = shallowRef(false)
const error = shallowRef('')

const sortedEntries = computed(() => [...entries.value].sort((a, b) =>
  (a.type === b.type ? a.name.localeCompare(b.name) : a.type === 'directory' ? -1 : 1)))

async function navigate(path) {
  loading.value = true
  error.value = ''
  try {
    const params = new URLSearchParams({ type: props.type === 'directory' ? 'directory' : 'any' })
    if (path) params.set('path', path)
    const data = await fetchJson(`./api/browse?${params}`)
    current.value = data.path ?? ''
    parent.value = data.parent ?? ''
    entries.value = data.entries ?? []
    selected.value = props.type === 'directory' ? current.value : ''
  } catch (e) {
    error.value = e.message || t('nova.common.load_failed')
  } finally {
    loading.value = false
  }
}

function pick(entry) {
  if (entry.type === 'directory') navigate(entry.path)
  else selected.value = entry.path
}

function confirm() {
  if (!selected.value) return
  emit('select', selected.value)
  open.value = false
}

watch(open, (isOpen) => {
  if (isOpen) navigate(props.start)
}, { immediate: true })
</script>

<template>
  <NvDialog v-model:open="open" :title="title" size="lg">
    <div class="nv-browse">
      <div class="nv-browse__bar">
        <NvButton size="sm" :disabled="!parent || parent === current" @click="navigate(parent)">
          <ArrowUp :size="16" aria-hidden="true" />{{ t('nova.settings.browse_up') }}
        </NvButton>
        <code class="nv-browse__path">{{ current || '/' }}</code>
      </div>
      <NvAlert v-if="error" variant="danger" live>{{ error }}</NvAlert>
      <div v-if="loading" class="nv-browse__list" aria-busy="true">
        <NvSkeleton v-for="n in 6" :key="n" height="36px" />
      </div>
      <ul v-else class="nv-browse__list" :aria-label="t('nova.settings.browse_contents')">
        <li v-for="entry in sortedEntries" :key="entry.path">
          <button type="button" :class="['nv-browse__entry', { 'nv-browse__entry--selected': selected === entry.path }]"
                  :aria-pressed="selected === entry.path ? 'true' : 'false'" @click="pick(entry)"
                  @dblclick="entry.type === 'file' && confirm()">
            <Folder v-if="entry.type === 'directory'" :size="16" aria-hidden="true" />
            <File v-else :size="16" aria-hidden="true" />
            <span>{{ entry.name }}</span>
          </button>
        </li>
        <li v-if="sortedEntries.length === 0" class="nv-browse__empty">{{ t('nova.settings.browse_empty') }}</li>
      </ul>
    </div>
    <template #footer>
      <NvButton @click="open = false">{{ t('nova.common.cancel') }}</NvButton>
      <NvButton variant="primary" :disabled="!selected" @click="confirm">{{ t('nova.settings.browse_choose') }}</NvButton>
    </template>
  </NvDialog>
</template>

<style>
@layer components {
  .nv-browse {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
  }

  .nv-browse__bar {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-width: 0;
  }

  .nv-browse__path {
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
    overflow-wrap: anywhere;
  }

  .nv-browse__list {
    display: flex;
    flex-direction: column;
    gap: 2px;
    max-height: 50vh;
    overflow-y: auto;
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-browse__entry {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    width: 100%;
    min-height: var(--nv-control-height-sm);
    padding: 0 var(--nv-space-3);
    border: 0;
    border-radius: var(--nv-radius-md);
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }

  .nv-browse__entry:hover {
    background: var(--nv-raised);
  }

  .nv-browse__entry--selected {
    background: var(--nv-accent-tint);
    color: var(--nv-accent-text);
  }

  .nv-browse__empty {
    padding: var(--nv-space-3);
    color: var(--nv-text-secondary);
  }
}
</style>

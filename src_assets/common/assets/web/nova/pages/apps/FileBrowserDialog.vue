<script setup>
/**
 * Browse the host's file system (GET /api/browse) and pick a file or folder.
 *
 * v-model:open — boolean.
 * Props: type ('any' | 'file' | 'executable' | 'directory'), title, startPath.
 * Emits: select(path) when the user confirms.
 */
import { computed, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { ArrowUp, File, Folder, HardDrive } from '@lucide/vue'
import NvDialog from '../../components/NvDialog.vue'
import NvButton from '../../components/NvButton.vue'
import NvTextField from '../../components/NvTextField.vue'
import NvAlert from '../../components/NvAlert.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import { fetchJson } from '../../api'

const open = defineModel('open', { type: Boolean, default: false })
const props = defineProps({
  type: { type: String, default: 'any' },
  title: { type: String, default: '' },
  startPath: { type: String, default: '' },
})
const emit = defineEmits(['select'])
const { t } = useI18n()

const current = ref('')
const parent = ref('')
const entries = ref([])
const typed = ref('')
const selected = ref('')
const loading = ref(false)
const error = ref('')

const canGoUp = computed(() => !loading.value && current.value && parent.value !== current.value)
const choice = computed(() => selected.value || typed.value)

async function navigate(path) {
  loading.value = true
  error.value = ''
  const params = new URLSearchParams({ type: props.type })
  if (path) params.set('path', path)
  try {
    const data = await fetchJson(`./api/browse?${params.toString()}`)
    current.value = data.path ?? ''
    parent.value = data.parent ?? ''
    entries.value = data.entries ?? []
    typed.value = data.path ?? ''
    selected.value = props.type === 'directory' ? (data.path ?? '') : ''
  } catch (e) {
    error.value = e.status === 400 || e.status === 404 ? t('nova.apps.browse_not_found') : t('nova.common.load_failed')
  } finally {
    loading.value = false
  }
}

function pick(entry) {
  if (entry.type === 'directory') {
    navigate(entry.path)
  } else {
    selected.value = entry.path
    typed.value = entry.path
  }
}

function confirm() {
  if (!choice.value) return
  emit('select', choice.value)
  open.value = false
}

watch(open, (isOpen) => {
  if (isOpen) {
    selected.value = props.startPath
    typed.value = props.startPath
    navigate(props.startPath)
  }
})
</script>

<template>
  <NvDialog v-model:open="open" :title="title || t('file_browser.title')" size="lg">
    <form class="nv-fb__path" @submit.prevent="navigate(typed)">
      <NvTextField v-model="typed" :label="t('nova.apps.browse_path')" hide-label mono @update:model-value="selected = typed" />
      <NvButton type="submit">{{ t('nova.apps.browse_go') }}</NvButton>
    </form>
    <div>
      <NvButton size="sm" variant="ghost" :disabled="!canGoUp" @click="navigate(parent)">
        <ArrowUp :size="16" aria-hidden="true" />{{ t('file_browser.up') }}
      </NvButton>
    </div>
    <NvAlert v-if="error" variant="danger" live>{{ error }}</NvAlert>
    <NvSkeleton v-if="loading" :lines="5" height="32px" />
    <ul v-else class="nv-fb__list" :aria-label="current || t('file_browser.root')">
      <li v-if="entries.length === 0" class="nv-fb__empty">{{ t('file_browser.empty') }}</li>
      <li v-for="entry in entries" :key="entry.path">
        <button type="button" :class="['nv-fb__entry', { 'nv-fb__entry--selected': selected === entry.path }]"
                :aria-pressed="entry.type === 'directory' ? null : String(selected === entry.path)"
                @click="pick(entry)" @dblclick="entry.type !== 'directory' && confirm()">
          <HardDrive v-if="!current && entry.type === 'directory'" :size="16" aria-hidden="true" />
          <Folder v-else-if="entry.type === 'directory'" :size="16" aria-hidden="true" />
          <File v-else :size="16" aria-hidden="true" />
          <span class="nv-fb__name">{{ entry.name }}</span>
        </button>
      </li>
    </ul>
    <template #footer>
      <code v-if="choice" class="nv-fb__choice">{{ choice }}</code>
      <NvButton @click="open = false">{{ t('nova.common.cancel') }}</NvButton>
      <NvButton variant="primary" :disabled="!choice" @click="confirm">{{ t('file_browser.select') }}</NvButton>
    </template>
  </NvDialog>
</template>

<style>
@layer components {
  .nv-fb__path {
    display: flex;
    align-items: flex-end;
    gap: var(--nv-space-2);
  }

  .nv-fb__path > :first-child {
    flex-grow: 1;
    min-width: 0;
  }

  .nv-fb__list {
    list-style: none;
    margin: 0;
    padding: var(--nv-space-1);
    max-height: 360px;
    overflow-y: auto;
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-md);
  }

  .nv-fb__empty {
    padding: var(--nv-space-4);
    text-align: center;
    color: var(--nv-text-secondary);
  }

  .nv-fb__entry {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    width: 100%;
    min-height: 36px;
    padding: 0 var(--nv-space-3);
    border: 0;
    border-radius: var(--nv-radius-sm);
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }

  .nv-fb__entry:hover {
    background: var(--nv-raised);
  }

  .nv-fb__entry--selected {
    background: var(--nv-accent-tint);
    color: var(--nv-accent-text);
  }

  .nv-fb__name {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-fb__choice {
    flex-grow: 1;
    min-width: 0;
    align-self: center;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }
}
</style>

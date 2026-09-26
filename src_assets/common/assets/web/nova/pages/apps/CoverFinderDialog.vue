<script setup>
/**
 * Search IGDB covers (via GameDB) for an app and save the chosen one on the host.
 *
 * v-model:open — boolean.
 * Props: initialQuery (usually the app name).
 * Emits: chosen(path) with the host path of the saved image.
 */
import { ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import NvDialog from '../../components/NvDialog.vue'
import NvButton from '../../components/NvButton.vue'
import NvTextField from '../../components/NvTextField.vue'
import NvAlert from '../../components/NvAlert.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import NvEmptyState from '../../components/NvEmptyState.vue'
import { saveCover, searchCovers } from './covers'

const open = defineModel('open', { type: Boolean, default: false })
const props = defineProps({
  initialQuery: { type: String, default: '' },
})
const emit = defineEmits(['chosen'])
const { t } = useI18n()

const query = ref('')
const results = ref([])
const searching = ref(false)
const searched = ref(false)
const saving = ref('')
const error = ref('')

async function search() {
  const term = query.value.trim()
  if (!term) return
  searching.value = true
  error.value = ''
  results.value = []
  try {
    results.value = await searchCovers(term)
  } catch {
    error.value = t('nova.apps.cover_search_failed')
  } finally {
    searching.value = false
    searched.value = true
  }
}

async function choose(cover) {
  saving.value = cover.key
  error.value = ''
  try {
    emit('chosen', await saveCover(cover))
    open.value = false
  } catch {
    error.value = t('nova.apps.cover_save_failed')
  } finally {
    saving.value = ''
  }
}

watch(open, (isOpen) => {
  if (!isOpen) return
  query.value = props.initialQuery
  results.value = []
  searched.value = false
  error.value = ''
  if (query.value.trim()) search()
})
</script>

<template>
  <NvDialog v-model:open="open" :title="t('nova.apps.cover_title')" :description="t('nova.apps.cover_desc')" size="lg">
    <form class="nv-covers__search" @submit.prevent="search">
      <NvTextField v-model="query" :label="t('nova.apps.cover_query')" hide-label type="search"
                   :placeholder="t('nova.apps.cover_query')" />
      <NvButton type="submit" variant="primary" :loading="searching">{{ t('nova.apps.cover_search') }}</NvButton>
    </form>
    <NvAlert v-if="error" variant="danger" live>{{ error }}</NvAlert>
    <div v-if="searching" class="nv-covers__grid" aria-hidden="true">
      <NvSkeleton v-for="n in 4" :key="n" height="180px" radius="var(--nv-radius-md)" />
    </div>
    <NvEmptyState v-else-if="searched && !results.length && !error" compact :title="t('nova.apps.cover_none')"
                  :description="t('nova.apps.cover_none_desc')" />
    <ul v-else-if="results.length" class="nv-covers__grid" :aria-label="t('nova.apps.cover_results', { count: results.length })">
      <li v-for="cover in results" :key="cover.key">
        <button type="button" class="nv-covers__choice" :disabled="!!saving" :aria-busy="saving === cover.key ? 'true' : null"
                @click="choose(cover)">
          <img :src="cover.url" alt="" loading="lazy" class="nv-covers__img" />
          <span class="nv-covers__name">{{ cover.name }}</span>
        </button>
      </li>
    </ul>
    <template #footer>
      <NvButton @click="open = false">{{ t('nova.common.cancel') }}</NvButton>
    </template>
  </NvDialog>
</template>

<style>
@layer components {
  .nv-covers__search {
    display: flex;
    gap: var(--nv-space-2);
  }

  .nv-covers__search > :first-child {
    flex-grow: 1;
    min-width: 0;
  }

  .nv-covers__grid {
    list-style: none;
    margin: 0;
    padding: 0;
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(128px, 1fr));
    gap: var(--nv-space-3);
  }

  .nv-covers__choice {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    width: 100%;
    padding: var(--nv-space-2);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
    color: var(--nv-text);
    font: inherit;
    cursor: pointer;
  }

  .nv-covers__choice:hover:not(:disabled) {
    border-color: var(--nv-accent);
  }

  .nv-covers__choice:disabled {
    cursor: progress;
    opacity: 0.7;
  }

  .nv-covers__img {
    width: 100%;
    aspect-ratio: 3 / 4;
    object-fit: cover;
    border-radius: var(--nv-radius-sm);
    background: var(--nv-surface);
  }

  .nv-covers__name {
    font-size: var(--nv-text-sm);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
}
</style>

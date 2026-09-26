<script setup>
/**
 * Search, sort and grid/list controls above the app list, with a live result count.
 *
 * v-model:query (string), v-model:sort ('default' | 'asc' | 'desc'), v-model:view ('grid' | 'list').
 * Props: shown, total (counts for the summary).
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import NvTextField from '../../components/NvTextField.vue'
import NvSegmentedControl from '../../components/NvSegmentedControl.vue'

const query = defineModel('query', { type: String, default: '' })
const sort = defineModel('sort', { type: String, default: 'default' })
const view = defineModel('view', { type: String, default: 'grid' })
const props = defineProps({
  shown: { type: Number, required: true },
  total: { type: Number, required: true },
})
const { t } = useI18n()

const sortOptions = computed(() => [
  { value: 'default', label: t('nova.apps.sort_default') },
  { value: 'asc', label: t('nova.apps.sort_asc') },
  { value: 'desc', label: t('nova.apps.sort_desc') },
])
const viewOptions = computed(() => [
  { value: 'grid', label: t('nova.apps.view_grid') },
  { value: 'list', label: t('nova.apps.view_list') },
])
const summary = computed(() => (props.shown === props.total
  ? t('nova.apps.count', { n: props.total })
  : t('nova.apps.count_filtered', { shown: props.shown, n: props.total })))
</script>

<template>
  <div class="nv-apps-toolbar">
    <NvTextField v-model="query" type="search" :label="t('nova.apps.search')" hide-label :placeholder="t('nova.apps.search')"
                 class="nv-apps-toolbar__search" />
    <NvSegmentedControl v-model="sort" :label="t('nova.apps.sort')" :options="sortOptions" size="sm" />
    <NvSegmentedControl v-model="view" :label="t('nova.apps.view')" :options="viewOptions" size="sm" />
    <p class="nv-apps-toolbar__count" aria-live="polite">{{ summary }}</p>
  </div>
</template>

<style>
@layer components {
  .nv-apps-toolbar {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: var(--nv-space-3);
    margin-bottom: var(--nv-space-5);
  }

  .nv-apps-toolbar__search {
    flex: 1 1 240px;
    max-width: 360px;
  }

  .nv-apps-toolbar__count {
    margin: 0 0 0 auto;
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  @media (max-width: 600px) {
    .nv-apps-toolbar__search {
      flex-basis: 100%;
      max-width: none;
    }

    .nv-apps-toolbar__count {
      margin-left: 0;
    }
  }
}
</style>

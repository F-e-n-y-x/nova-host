<script setup>
/**
 * System / Light / Dark theme choice, saved in localStorage (see theme.js).
 */
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import NvSegmentedControl from './NvSegmentedControl.vue'
import { getThemePreference, setThemePreference } from '../../theme'

const { t } = useI18n()
const choice = ref(getThemePreference())
const options = computed(() => [
  { value: 'system', label: t('nova.theme.system') },
  { value: 'light', label: t('nova.theme.light') },
  { value: 'dark', label: t('nova.theme.dark') },
])

function update(value) {
  choice.value = value
  setThemePreference(value)
}
</script>

<template>
  <NvSegmentedControl :model-value="choice" :options="options" :label="t('nova.theme.label')" size="sm"
                      @update:model-value="update" />
</template>

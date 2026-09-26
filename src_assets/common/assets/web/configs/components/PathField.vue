<script setup>
/**
 * Text field for a file path with a Browse button that picks a file on the host.
 *
 * v-model: path string.
 * Props: label (accessible name), placeholder, error, type ('file' | 'directory').
 */
import { shallowRef } from 'vue'
import { useI18n } from 'vue-i18n'
import { FolderOpen } from '@lucide/vue'
import NvButton from '../../nova/components/NvButton.vue'
import NvTextField from '../../nova/components/NvTextField.vue'
import PathBrowserDialog from './PathBrowserDialog.vue'

const model = defineModel({ type: String, default: '' })
defineProps({
  label: { type: String, required: true },
  placeholder: { type: String, default: '' },
  error: { type: String, default: '' },
  type: { type: String, default: 'file' },
})

const { t } = useI18n()
const browsing = shallowRef(false)
</script>

<template>
  <div class="nv-path-field">
    <NvTextField v-model="model" :label="label" hide-label :placeholder="placeholder" mono :error="error" />
    <NvButton size="md" @click="browsing = true">
      <FolderOpen :size="16" aria-hidden="true" />{{ t('nova.settings.browse') }}
    </NvButton>
    <PathBrowserDialog v-model:open="browsing" :title="t('nova.settings.browse_title', { name: label })" :type="type"
                       :start="model" @select="(path) => (model = path)" />
  </div>
</template>

<style>
@layer components {
  .nv-path-field {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-2);
  }

  .nv-path-field .nv-field {
    flex-grow: 1;
    min-width: 0;
  }
}
</style>

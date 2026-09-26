<script setup>
/**
 * Monospace text field with a Browse button beside it (for commands, folders and files).
 *
 * v-model: string.
 * Props: label (required), hint.
 * Emits: browse when the Browse button is pressed; the parent opens the file browser.
 */
import { useI18n } from 'vue-i18n'
import { FolderOpen } from '@lucide/vue'
import NvTextField from '../../components/NvTextField.vue'
import NvIconButton from '../../components/NvIconButton.vue'

const model = defineModel({ type: String, default: '' })
defineProps({
  label: { type: String, required: true },
  hint: { type: String, default: '' },
})
defineEmits(['browse'])
const { t } = useI18n()
</script>

<template>
  <div class="nv-path-field">
    <NvTextField v-model="model" :label="label" :hint="hint" mono class="nv-path-field__input" />
    <NvIconButton variant="secondary" :label="t('nova.apps.browse_for', { field: label })" class="nv-path-field__browse"
                  @click="$emit('browse')">
      <FolderOpen :size="18" aria-hidden="true" />
    </NvIconButton>
  </div>
</template>

<style>
@layer components {
  .nv-path-field {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-2);
  }

  .nv-path-field__input {
    flex-grow: 1;
    min-width: 0;
  }

  /* Line the button up with the input, below the field's label. */
  .nv-path-field__browse {
    flex-shrink: 0;
    margin-top: calc(var(--nv-text-md) * var(--nv-leading) + var(--nv-space-1));
  }
}
</style>

<script setup>
/**
 * Monospace text field with a Browse button that opens the host file browser (GET /api/browse).
 *
 * v-model: string (path or command).
 * Props: label (required), hint, error, type ('any' | 'file' | 'executable' | 'directory'),
 *        browseTitle (dialog title; defaults by type), disabled.
 * Emits: picked(path) after a selection from the browser.
 */
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { FolderOpen } from '@lucide/vue'
import NvTextField from './NvTextField.vue'
import NvFileBrowser from './NvFileBrowser.vue'

defineOptions({ inheritAttrs: false })

const model = defineModel({ type: String, default: '' })
const props = defineProps({
  label: { type: String, required: true },
  hint: { type: String, default: '' },
  error: { type: String, default: '' },
  type: { type: String, default: 'any' },
  browseTitle: { type: String, default: '' },
  disabled: { type: Boolean, default: false },
})
const emit = defineEmits(['picked'])

const { t } = useI18n()
const browsing = ref(false)
const title = computed(() => props.browseTitle || {
  directory: t('nova.common.browse_folder'),
  executable: t('nova.common.browse_program'),
  file: t('nova.common.browse_file'),
}[props.type] || t('nova.common.browse_file'))

function onSelect(path) {
  model.value = path
  emit('picked', path)
}
</script>

<template>
  <div class="nv-path-field" :class="$attrs.class">
    <NvTextField v-model="model" v-bind="{ ...$attrs, class: undefined }" :label="label" :hint="hint" :error="error"
                 :disabled="disabled" mono spellcheck="false" autocapitalize="off" class="nv-path-field__input" />
    <button type="button" class="nv-path-field__browse" :disabled="disabled"
            :aria-label="t('nova.common.browse_for', { field: label })" :title="t('nova.common.browse_for', { field: label })"
            @click="browsing = true">
      <FolderOpen :size="16" aria-hidden="true" />
    </button>
    <NvFileBrowser v-model:open="browsing" :type="type" :title="title" :start-path="model" @select="onSelect" />
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

  /* Label row (13px/20) + 6px gap puts the input's top at 26px. */
  .nv-path-field__browse {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
    width: var(--nv-control-height);
    height: var(--nv-control-height);
    margin-top: 26px;
    padding: 0;
    border: 1px solid var(--nv-border-strong);
    border-radius: var(--nv-radius-md);
    background: transparent;
    color: var(--nv-text-secondary);
    cursor: pointer;
  }

  .nv-path-field__browse:hover:not(:disabled) {
    background: var(--nv-raised);
    color: var(--nv-text);
  }

  @media (max-width: 767px) {
    .nv-path-field__browse {
      width: var(--nv-control-height-lg);
      height: var(--nv-control-height-lg);
    }
  }
}
</style>

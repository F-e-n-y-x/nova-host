<script setup>
/**
 * Confirmation for a destructive action: Cancel plus a filled danger button.
 *
 * v-model:open — boolean.
 * Props: title (required), description, confirmLabel (required), busy (spinner on the confirm button).
 * Emits: confirm.
 */
import { useI18n } from 'vue-i18n'
import NvDialog from '../../components/NvDialog.vue'
import NvButton from '../../components/NvButton.vue'

const open = defineModel('open', { type: Boolean, default: false })
defineProps({
  title: { type: String, required: true },
  description: { type: String, default: '' },
  confirmLabel: { type: String, required: true },
  busy: { type: Boolean, default: false },
})
defineEmits(['confirm'])
const { t } = useI18n()
</script>

<template>
  <NvDialog v-model:open="open" :title="title" :description="description">
    <template #footer>
      <NvButton autofocus @click="open = false">{{ t('nova.common.cancel') }}</NvButton>
      <NvButton variant="danger-solid" :loading="busy" @click="$emit('confirm')">{{ confirmLabel }}</NvButton>
    </template>
  </NvDialog>
</template>

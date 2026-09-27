<script setup>
/**
 * Confirmation for destructive or disruptive actions (SPEC §5 Dialog): the title is a question
 * naming the object ("Unpair Pixel 9 Pro?"), the description states the consequence, Cancel gets
 * initial focus, and the confirm button repeats the verb ("Unpair").
 *
 * v-model:open — boolean.
 * Props: title (required), description (required), confirmLabel (required; the verb),
 *        variant ('danger' | 'primary'), loading (confirm pending), error (shown inline),
 *        requireCheck (label of a checkbox that must be ticked — bulk/irreversible actions),
 *        requireText (text the user must type, e.g. the device name).
 * Slots: default (extra body content).
 * Emits: confirm (the parent performs the action, then closes the dialog or sets error).
 */
import { computed, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import NvDialog from './NvDialog.vue'
import NvButton from './NvButton.vue'
import NvCheckbox from './NvCheckbox.vue'
import NvTextField from './NvTextField.vue'
import NvAlert from './NvAlert.vue'

const open = defineModel('open', { type: Boolean, default: false })
const props = defineProps({
  title: { type: String, required: true },
  description: { type: String, required: true },
  confirmLabel: { type: String, required: true },
  variant: { type: String, default: 'danger' },
  loading: { type: Boolean, default: false },
  error: { type: String, default: '' },
  requireCheck: { type: String, default: '' },
  requireText: { type: String, default: '' },
})
const emit = defineEmits(['confirm'])

const { t } = useI18n()
const checked = ref(false)
const typed = ref('')
const ready = computed(() => (!props.requireCheck || checked.value) &&
  (!props.requireText || typed.value.trim() === props.requireText))

watch(open, (isOpen) => {
  if (isOpen) {
    checked.value = false
    typed.value = ''
  }
})

function confirm() {
  if (ready.value && !props.loading) emit('confirm')
}
</script>

<template>
  <NvDialog v-model:open="open" :title="title" :description="description" size="sm" initial-focus="[data-nv-cancel]"
            :persistent="loading">
    <slot />
    <NvCheckbox v-if="requireCheck" v-model="checked" :label="requireCheck" />
    <NvTextField v-if="requireText" v-model="typed" :label="t('nova.common.type_to_confirm', { text: requireText })"
                 autocomplete="off" spellcheck="false" @keydown.enter.prevent="confirm" />
    <NvAlert v-if="error" variant="danger" live>{{ error }}</NvAlert>
    <template #footer>
      <NvButton data-nv-cancel :disabled="loading" @click="open = false">{{ t('nova.common.cancel') }}</NvButton>
      <NvButton :variant="variant === 'danger' ? 'danger-solid' : 'primary'" :loading="loading" :disabled="!ready"
                @click="confirm">{{ confirmLabel }}</NvButton>
    </template>
  </NvDialog>
</template>

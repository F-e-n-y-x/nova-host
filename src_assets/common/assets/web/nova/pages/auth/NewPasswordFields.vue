<script setup>
/**
 * New password + confirmation. Each field has its own show/hide button (NvTextField);
 * errors are shown inline (already translated by the parent).
 *
 * v-model:password, v-model:confirm. Props: passwordLabel, confirmLabel, passwordError, confirmError.
 * Emits: touch('password' | 'confirm') when a field loses focus.
 */
import { useI18n } from 'vue-i18n'
import NvTextField from '../../components/NvTextField.vue'
import { MIN_PASSWORD_LENGTH } from './useCredentialsForm'

const password = defineModel('password', { type: String, default: '' })
const confirm = defineModel('confirm', { type: String, default: '' })
defineProps({
  passwordLabel: { type: String, required: true },
  confirmLabel: { type: String, required: true },
  passwordError: { type: String, default: '' },
  confirmError: { type: String, default: '' },
})
defineEmits(['touch'])

const { t } = useI18n()
</script>

<template>
  <NvTextField v-model="password" type="password" :label="passwordLabel" autocomplete="new-password" required
               :hint="t('nova.auth.password_hint', { min: MIN_PASSWORD_LENGTH })" :error="passwordError"
               @focusout="$emit('touch', 'password')" />
  <NvTextField v-model="confirm" type="password" :label="confirmLabel" autocomplete="new-password" required
               :error="confirmError" @focusout="$emit('touch', 'confirm')" />
</template>

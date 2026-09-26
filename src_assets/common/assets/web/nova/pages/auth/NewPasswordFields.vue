<script setup>
/**
 * New password + confirmation with a "Show passwords" toggle. Errors are shown inline
 * (already translated by the parent).
 *
 * v-model:password, v-model:confirm. Props: passwordLabel, confirmLabel, passwordError, confirmError.
 * Emits: touch('password' | 'confirm') when a field loses focus.
 */
import { computed, shallowRef, useId } from 'vue'
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
const reveal = shallowRef(false)
const revealId = useId()
const fieldType = computed(() => (reveal.value ? 'text' : 'password'))
</script>

<template>
  <NvTextField v-model="password" :type="fieldType" :label="passwordLabel" autocomplete="new-password" required
               :hint="t('nova.auth.password_hint', { min: MIN_PASSWORD_LENGTH })" :error="passwordError"
               @focusout="$emit('touch', 'password')" />
  <NvTextField v-model="confirm" :type="fieldType" :label="confirmLabel" autocomplete="new-password" required
               :error="confirmError" @focusout="$emit('touch', 'confirm')" />
  <label class="nv-reveal" :for="revealId">
    <input :id="revealId" v-model="reveal" type="checkbox" class="nv-reveal__box">
    <span>{{ t('nova.auth.show_passwords') }}</span>
  </label>
</template>

<style>
@layer components {
  .nv-reveal {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-2);
    min-height: var(--nv-control-height-sm);
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
    cursor: pointer;
    align-self: flex-start;
  }

  .nv-reveal__box {
    width: 18px;
    height: 18px;
    margin: 0;
    accent-color: var(--nv-accent);
  }
}
</style>

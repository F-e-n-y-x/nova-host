<script setup>
/**
 * First-run setup: create the web UI username and password.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import NvTextField from './nova/components/NvTextField.vue'
import NvButton from './nova/components/NvButton.vue'
import NvAlert from './nova/components/NvAlert.vue'
import AuthLayout from './nova/pages/auth/AuthLayout.vue'
import NewPasswordFields from './nova/pages/auth/NewPasswordFields.vue'
import { GENERIC_ERROR, useCredentialsForm } from './nova/pages/auth/useCredentialsForm'

const { t } = useI18n()
const form = useCredentialsForm({
  usernameRequired: true,
  toBody: (v) => ({ newUsername: v.username.trim(), newPassword: v.password, confirmNewPassword: v.confirm }),
})
const { values, errors, saving, saved, serverError } = form

const fieldError = (field) => computed(() => (errors.value[field] ? t(errors.value[field].key, errors.value[field].params || {}) : ''))
const usernameError = fieldError('username')
const passwordError = fieldError('password')
const confirmError = fieldError('confirm')
const serverMessage = computed(() => (serverError.value === GENERIC_ERROR ? t('nova.auth.server_error') : serverError.value))
</script>

<template>
  <AuthLayout :title="t('nova.auth.welcome_title')" :intro="t('nova.auth.welcome_intro')">
    <NvAlert v-if="saved" variant="success" live :title="t('nova.auth.saved')">
      <template #actions><NvButton variant="primary" size="sm" href="./">{{ t('nova.auth.continue') }}</NvButton></template>
    </NvAlert>
    <form v-else class="nv-auth-form" novalidate @submit.prevent="form.submit()">
      <p class="nv-secondary nv-auth-form__note">{{ t('nova.auth.welcome_scope') }}</p>
      <NvTextField v-model="values.username" :label="t('nova.auth.username')" :placeholder="t('nova.auth.username_placeholder')"
                   autocomplete="username" required :error="usernameError" @focusout="form.touch('username')" />
      <NewPasswordFields v-model:password="values.password" v-model:confirm="values.confirm"
                         :password-label="t('nova.auth.password')" :confirm-label="t('nova.auth.confirm_password')"
                         :password-error="passwordError" :confirm-error="confirmError" @touch="form.touch" />
      <NvAlert v-if="serverError" variant="danger" live>{{ t('nova.auth.error', { error: serverMessage }) }}</NvAlert>
      <p class="nv-secondary nv-auth-form__note">{{ t('nova.auth.welcome_keep') }}</p>
      <NvButton type="submit" variant="primary" block :loading="saving">{{ t('nova.auth.create') }}</NvButton>
    </form>
  </AuthLayout>
</template>

<style>
@layer components {
  .nv-auth-form {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
  }

  .nv-auth-form__note {
    margin: 0;
    font-size: var(--nv-text-sm);
  }
}
</style>

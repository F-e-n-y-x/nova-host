<script setup>
/**
 * Change the web UI username and password.
 */
import { computed, useTemplateRef } from 'vue'
import { useI18n } from 'vue-i18n'
import NvPage from './nova/components/NvPage.vue'
import NvCard from './nova/components/NvCard.vue'
import NvTextField from './nova/components/NvTextField.vue'
import NvButton from './nova/components/NvButton.vue'
import NvAlert from './nova/components/NvAlert.vue'
import NewPasswordFields from './nova/pages/auth/NewPasswordFields.vue'
import SessionsCard from './nova/pages/auth/SessionsCard.vue'
import { GENERIC_ERROR, useCredentialsForm } from './nova/pages/auth/useCredentialsForm'

const { t } = useI18n()
const formEl = useTemplateRef('formEl')
const form = useCredentialsForm({
  root: () => formEl.value,
  usernameRequired: false,
  toBody: (v) => ({
    currentUsername: v.currentUsername.trim(),
    currentPassword: v.currentPassword,
    newUsername: v.username.trim(),
    newPassword: v.password,
    confirmNewPassword: v.confirm,
  }),
})
const { values, errors, saving, saved, serverError } = form

const fieldError = (field) => computed(() => (errors.value[field] ? t(errors.value[field].key, errors.value[field].params || {}) : ''))
const passwordError = fieldError('password')
const confirmError = fieldError('confirm')
const serverMessage = computed(() => (serverError.value === GENERIC_ERROR ? t('nova.auth.server_error') : serverError.value))
</script>

<template>
  <NvPage :title="t('nova.auth.change_title')">
    <template #subtitle><span>{{ t('nova.auth.change_intro') }}</span></template>
    <NvAlert v-if="saved" variant="success" live :title="t('nova.auth.saved')">
      <template #actions><NvButton variant="primary" size="sm" href="./">{{ t('nova.auth.continue') }}</NvButton></template>
    </NvAlert>
    <form v-else ref="formEl" class="nv-password" novalidate @submit.prevent="form.submit()">
      <NvCard>
        <fieldset class="nv-password__set">
          <legend class="nv-password__legend">{{ t('nova.auth.current') }}</legend>
          <NvTextField v-model="values.currentUsername" :label="t('nova.auth.current_username')" autocomplete="username" required />
          <NvTextField v-model="values.currentPassword" type="password" :label="t('nova.auth.current_password')"
                       autocomplete="current-password" required />
        </fieldset>
        <fieldset class="nv-password__set">
          <legend class="nv-password__legend">{{ t('nova.auth.new') }}</legend>
          <NvTextField v-model="values.username" :label="t('nova.auth.new_username')" :hint="t('nova.auth.new_username_hint')"
                       autocomplete="off" />
          <NewPasswordFields v-model:password="values.password" v-model:confirm="values.confirm"
                             :password-label="t('nova.auth.new_password')" :confirm-label="t('nova.auth.confirm_new_password')"
                             :password-error="passwordError" :confirm-error="confirmError" @touch="form.touch" />
        </fieldset>
      </NvCard>
      <NvAlert v-if="serverError" variant="danger" live>{{ t('nova.auth.error', { error: serverMessage }) }}</NvAlert>
      <div>
        <NvButton type="submit" variant="primary" :loading="saving">{{ t('nova.auth.save') }}</NvButton>
      </div>
    </form>
    <SessionsCard v-if="!saved" class="nv-password__sessions" />
  </NvPage>
</template>

<style>
@layer components {
  .nv-password {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-5);
    max-width: 560px;
  }

  .nv-password__set {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
    margin: 0;
    padding: 0;
    border: 0;
    min-width: 0;
  }

  .nv-password__set + .nv-password__set {
    padding-top: var(--nv-space-5);
    border-top: 1px solid var(--nv-border);
  }

  .nv-password__sessions {
    margin-top: var(--nv-space-6);
  }

  .nv-password__legend {
    padding: 0;
    margin-bottom: var(--nv-space-3);
    font-size: var(--nv-text-md);
    font-weight: 600;
  }
}
</style>

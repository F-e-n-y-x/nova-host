<script setup>
/**
 * Change the web UI username and password.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import NvPage from './nova/components/NvPage.vue'
import NvCard from './nova/components/NvCard.vue'
import NvTextField from './nova/components/NvTextField.vue'
import NvButton from './nova/components/NvButton.vue'
import NvAlert from './nova/components/NvAlert.vue'
import NewPasswordFields from './nova/pages/auth/NewPasswordFields.vue'
import { GENERIC_ERROR, useCredentialsForm } from './nova/pages/auth/useCredentialsForm'

const { t } = useI18n()
const form = useCredentialsForm({
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
    <form v-else class="nv-password" novalidate @submit.prevent="form.submit()">
      <div class="nv-password__grid">
        <NvCard :title="t('nova.auth.current')">
          <NvTextField v-model="values.currentUsername" :label="t('nova.auth.current_username')" autocomplete="username" required />
          <NvTextField v-model="values.currentPassword" type="password" :label="t('nova.auth.current_password')"
                       autocomplete="current-password" required />
        </NvCard>
        <NvCard :title="t('nova.auth.new')">
          <NvTextField v-model="values.username" :label="t('nova.auth.new_username')" :hint="t('nova.auth.new_username_hint')"
                       autocomplete="off" />
          <NewPasswordFields v-model:password="values.password" v-model:confirm="values.confirm"
                             :password-label="t('nova.auth.new_password')" :confirm-label="t('nova.auth.confirm_new_password')"
                             :password-error="passwordError" :confirm-error="confirmError" @touch="form.touch" />
        </NvCard>
      </div>
      <NvAlert v-if="serverError" variant="danger" live>{{ t('nova.auth.error', { error: serverMessage }) }}</NvAlert>
      <div>
        <NvButton type="submit" variant="primary" :loading="saving">{{ t('nova.auth.save') }}</NvButton>
      </div>
    </form>
  </NvPage>
</template>

<style>
@layer components {
  .nv-password {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-5);
  }

  .nv-password__grid {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: var(--nv-space-5);
    align-items: start;
  }

  @media (max-width: 899px) {
    .nv-password__grid {
      grid-template-columns: minmax(0, 1fr);
      gap: var(--nv-space-4);
    }
  }
}
</style>

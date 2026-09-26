<script setup>
/**
 * Shown after signing out.
 */
import { useI18n } from 'vue-i18n'
import NvButton from './nova/components/NvButton.vue'
import AuthLayout from './nova/pages/auth/AuthLayout.vue'

const { t } = useI18n()
</script>

<template>
  <AuthLayout :title="t('nova.auth.signed_out_title')" :intro="t('nova.auth.signed_out_desc')">
    <NvButton variant="primary" block href="./">{{ t('nova.auth.sign_in') }}</NvButton>
  </AuthLayout>
</template>

<script setup>
/**
 * Logs & diagnostics page: the Windows virtual input panel (Windows only), one-click
 * fixes, and the host log.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import NvPage from '../components/NvPage.vue'
import VirtualInputCard from './logs/VirtualInputCard.vue'
import DiagnosticsPanel from './logs/DiagnosticsPanel.vue'
import LogViewer from './logs/LogViewer.vue'
import { getConfig, useAsync } from '../api'

const { t } = useI18n()
const config = useAsync(() => getConfig())
const platform = computed(() => config.data.value?.platform || '')
const gamepadDriver = computed(() => config.data.value?.gamepad_driver || '')
</script>

<template>
  <NvPage :title="t('nova.logs.title')" wide>
    <template #subtitle><span>{{ t('nova.logs.intro') }}</span></template>
    <VirtualInputCard v-if="platform === 'windows'" :gamepad-driver="gamepadDriver" />
    <DiagnosticsPanel :platform="platform" :loading="config.loading.value" />
    <LogViewer />
  </NvPage>
</template>

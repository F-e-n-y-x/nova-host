<script setup>
/**
 * Logs & diagnostics page: the host log on the left; the host's health checks and one-click
 * fixes on the right (stacked below on narrow screens). Windows adds the virtual input panel.
 */
import { computed, shallowRef } from 'vue'
import { useI18n } from 'vue-i18n'
import { Download } from '@lucide/vue'
import NvButton from '../components/NvButton.vue'
import NvPage from '../components/NvPage.vue'
import VirtualInputCard from './logs/VirtualInputCard.vue'
import DiagnosticsPanel from './logs/DiagnosticsPanel.vue'
import HealthPanel from './logs/HealthPanel.vue'
import LogViewer from './logs/LogViewer.vue'
import { getConfig, useAsync } from '../api'

const { t } = useI18n()
const config = useAsync(() => getConfig())
const platform = computed(() => config.data.value?.platform || '')
const gamepadDriver = computed(() => config.data.value?.gamepad_driver || '')
const viewer = shallowRef(null)
</script>

<template>
  <NvPage :title="t('nova.logs.title')" wide>
    <template #actions>
      <NvButton variant="primary" :disabled="!viewer?.canDownload" @click="viewer?.download()">
        <Download :size="16" aria-hidden="true" />{{ t('nova.logs.download') }}
      </NvButton>
    </template>
    <VirtualInputCard v-if="platform === 'windows'" :gamepad-driver="gamepadDriver" />
    <div class="nv-logs-layout">
      <LogViewer ref="viewer" class="nv-logs-layout__log" />
      <aside class="nv-logs-layout__side" :aria-label="t('nova.logs.diagnostics')">
        <HealthPanel />
        <DiagnosticsPanel :platform="platform" :loading="config.loading.value" />
      </aside>
    </div>
  </NvPage>
</template>

<style>
@layer components {
  .nv-logs-layout {
    display: grid;
    grid-template-columns: minmax(0, 1fr) 340px;
    gap: var(--nv-space-5);
    align-items: start;
  }

  .nv-logs-layout__side {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-5);
    position: sticky;
    top: var(--nv-space-5);
  }

  @media (max-width: 1279px) {
    .nv-logs-layout {
      grid-template-columns: minmax(0, 1fr);
    }

    .nv-logs-layout__side {
      position: static;
      order: -1;
    }
  }
}
</style>

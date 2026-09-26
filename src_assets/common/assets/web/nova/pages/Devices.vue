<script setup>
/**
 * Devices: paired clients. Read-only for now; unpairing still lives on Logs & diagnostics.
 */
import { useI18n } from 'vue-i18n'
import NvPage from '../components/NvPage.vue'
import NvCard from '../components/NvCard.vue'
import NvButton from '../components/NvButton.vue'
import NvBadge from '../components/NvBadge.vue'
import NvEmptyState from '../components/NvEmptyState.vue'
import NvSkeleton from '../components/NvSkeleton.vue'
import NvAlert from '../components/NvAlert.vue'
import { fetchJson, useAsync } from '../api'

const { t } = useI18n()
const clients = useAsync(async () => (await fetchJson('./api/clients/list')).named_certs || [])
</script>

<template>
  <NvPage :title="t('nova.devices.title')">
    <template #subtitle><span>{{ t('nova.devices.intro') }}</span></template>
    <template #actions><NvButton variant="primary" to="/pair">{{ t('nova.nav.pair') }}</NvButton></template>
    <NvCard>
      <div v-if="clients.loading.value" aria-busy="true"><NvSkeleton :lines="4" height="18px" /></div>
      <NvAlert v-else-if="clients.error.value" variant="danger" :title="t('nova.common.load_failed')">
        <template #actions><NvButton size="sm" variant="secondary" @click="clients.reload()">{{ t('nova.common.retry') }}</NvButton></template>
      </NvAlert>
      <NvEmptyState v-else-if="!clients.data.value?.length" :title="t('nova.dashboard.no_devices')" :description="t('nova.dashboard.no_devices_desc')" />
      <ul v-else class="nv-list">
        <li v-for="client in clients.data.value" :key="client.uuid" class="nv-list__row">
          <span class="nv-list__name">{{ client.name || client.uuid }}</span>
          <NvBadge :variant="client.enabled === false ? 'neutral' : 'accent'">
            {{ client.enabled === false ? t('nova.devices.status_disabled') : t('nova.devices.status_enabled') }}
          </NvBadge>
        </li>
      </ul>
    </NvCard>
  </NvPage>
</template>

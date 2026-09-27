<script setup>
/**
 * Devices card (Final_Dashboard): paired devices with a status line — "Streaming now", "Disabled",
 * or "Last connected <when>" plus the permission level when it isn't full access. Rows link to the
 * device's panel on the Devices page.
 *
 * Props: devices (/api/clients/list named_certs, or null), loading, error, streamingUuids (Set).
 * Emits: retry.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { Smartphone } from '@lucide/vue'
import NvCard from '../../components/NvCard.vue'
import NvButton from '../../components/NvButton.vue'
import NvAlert from '../../components/NvAlert.vue'
import NvEmptyState from '../../components/NvEmptyState.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import { absoluteTime, permissionPreset, relativeTime } from '../../devices/format'

const PREVIEW = 4

const props = defineProps({
  devices: { type: Array, default: null },
  loading: { type: Boolean, default: false },
  error: { type: [Object, Error, String], default: null },
  streamingUuids: { type: Object, default: () => new Set() },
})
defineEmits(['retry'])

const { t, locale } = useI18n()

const rows = computed(() => (props.devices || []).map((d) => {
  const streaming = d.connected === true || props.streamingUuids.has(d.uuid)
  const preset = permissionPreset(d.permissions)
  let sub
  let tone = 'muted'
  if (streaming) {
    sub = t('nova.overview.streaming_now')
    tone = 'success'
  } else if (d.enabled === false) {
    sub = t('nova.devices.status_disabled')
  } else {
    sub = d.last_connected_at
      ? t('nova.overview.last_connected', { when: relativeTime(d.last_connected_at, locale.value) })
      : t('nova.overview.never_connected')
    if (preset !== 'full') sub += ` · ${t(`nova.overview.preset_${preset}`)}`
  }
  return { ...d, streaming, sub, tone, title: absoluteTime(d.last_connected_at, locale.value) }
}).sort((a, b) => Number(b.streaming) - Number(a.streaming)))

const shown = computed(() => rows.value.slice(0, PREVIEW))
</script>

<template>
  <NvCard :title="t('nova.overview.devices_title')" flush class="nv-devc">
    <template #actions><RouterLink to="/devices">{{ t('nova.overview.manage') }}</RouterLink></template>

    <div v-if="loading" class="nv-devc__pad" aria-busy="true">
      <span class="nv-visually-hidden">{{ t('nova.overview.loading_devices') }}</span>
      <NvSkeleton v-for="i in 3" :key="i" height="32px" />
    </div>
    <div v-else-if="error" class="nv-devc__pad">
      <NvAlert variant="danger" :title="t('nova.overview.devices_failed')">
        <template #actions><NvButton size="sm" @click="$emit('retry')">{{ t('nova.common.retry') }}</NvButton></template>
      </NvAlert>
    </div>
    <NvEmptyState v-else-if="!rows.length" compact :title="t('nova.overview.no_devices')" :description="t('nova.overview.no_devices_desc')">
      <template #icon><Smartphone :size="28" /></template>
      <template #actions><NvButton size="sm" variant="primary" to="/pair">{{ t('nova.overview.pair_device') }}</NvButton></template>
    </NvEmptyState>
    <ul v-else class="nv-devc__list">
      <li v-for="d in shown" :key="d.uuid">
        <RouterLink :to="`/devices/${encodeURIComponent(d.uuid)}`" class="nv-devc__row">
          <span class="nv-devc__icon" aria-hidden="true"><Smartphone :size="16" /></span>
          <span class="nv-devc__text">
            <span class="nv-devc__name" :title="d.name">{{ d.name || d.uuid }}</span>
            <span :class="['nv-devc__sub', `nv-devc__sub--${d.tone}`]" :title="d.title || null">{{ d.sub }}</span>
          </span>
        </RouterLink>
      </li>
    </ul>
    <p v-if="rows.length > shown.length" class="nv-devc__more">
      <RouterLink to="/devices">{{ t('nova.overview.more_devices', { n: rows.length - shown.length }) }}</RouterLink>
    </p>
  </NvCard>
</template>

<style>
@layer components {
  .nv-devc__pad {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
    padding: var(--nv-space-4) var(--nv-space-5);
  }

  .nv-devc__list {
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-devc__row {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 56px;
    padding: 0 var(--nv-space-5);
    border-bottom: 1px solid var(--nv-divider);
    color: inherit;
    text-decoration: none;
  }

  .nv-devc__list li:last-child .nv-devc__row {
    border-bottom: 0;
  }

  .nv-devc__row:hover {
    background: var(--nv-raised);
  }

  .nv-devc__icon {
    display: flex;
    width: 32px;
    height: 32px;
    flex-shrink: 0;
    align-items: center;
    justify-content: center;
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
    color: var(--nv-text-secondary);
  }

  .nv-devc__text {
    display: flex;
    flex-direction: column;
    min-width: 0;
    line-height: 1.3;
  }

  .nv-devc__name {
    font-weight: 500;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-devc__sub {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
  }

  .nv-devc__sub--success {
    color: var(--nv-success);
  }

  .nv-devc__more {
    margin: 0;
    padding: var(--nv-space-3) var(--nv-space-5);
    border-top: 1px solid var(--nv-divider);
    font-size: var(--nv-text-sm);
  }
}
</style>

<script setup>
/**
 * One paired device in the list: name (opens details), a status line, status badges,
 * an allow/block switch and a details button.
 */
import { computed, useId } from 'vue'
import { useI18n } from 'vue-i18n'
import { ChevronRight } from '@lucide/vue'
import NvBadge from '../components/NvBadge.vue'
import NvSwitch from '../components/NvSwitch.vue'
import NvIconButton from '../components/NvIconButton.vue'
import { absoluteTime, permissionPreset, relativeTime } from './format'

const props = defineProps({
  device: { type: Object, required: true },
  busy: { type: Boolean, default: false },
})
const emit = defineEmits(['open', 'toggle'])

const { t, locale } = useI18n()
const id = useId()

const name = computed(() => props.device.name || t('nova.devices.unnamed'))
const enabled = computed(() => props.device.enabled !== false)
const preset = computed(() => permissionPreset(props.device.permissions))
const statusLine = computed(() => {
  if (props.device.connected) return t('nova.devices.streaming_now')
  if (props.device.last_connected_at) return t('nova.devices.last_connected', { when: relativeTime(props.device.last_connected_at, locale.value) })
  return t('nova.devices.never_connected')
})
const statusTitle = computed(() => absoluteTime(props.device.last_connected_at, locale.value) || null)
</script>

<template>
  <li class="nv-device-row">
    <div class="nv-device-row__main">
      <button :id="`${id}-name`" type="button" class="nv-device-row__name" :aria-describedby="`${id}-status`"
              @click="emit('open', device.uuid)">{{ name }}</button>
      <span :id="`${id}-status`" class="nv-device-row__status" :title="statusTitle">{{ statusLine }}</span>
    </div>
    <div class="nv-device-row__badges">
      <NvBadge v-if="device.connected" variant="success">{{ t('nova.devices.streaming_now') }}</NvBadge>
      <NvBadge v-if="!enabled">{{ t('nova.devices.status_disabled') }}</NvBadge>
      <NvBadge v-if="preset !== 'full'">{{ t(`nova.devices.preset_${preset}`) }}</NvBadge>
    </div>
    <div class="nv-device-row__actions">
      <span :id="`${id}-allow`" class="nv-visually-hidden">{{ t('nova.devices.allow_label') }}</span>
      <NvSwitch :model-value="enabled" :disabled="busy" :labelledby="`${id}-allow ${id}-name`"
                @update:model-value="(value) => emit('toggle', device.uuid, value)" />
      <NvIconButton :label="t('nova.devices.details_for', { name })" @click="emit('open', device.uuid)">
        <ChevronRight :size="18" aria-hidden="true" />
      </NvIconButton>
    </div>
  </li>
</template>

<style scoped>
@layer components {
  .nv-device-row {
    display: flex;
    align-items: center;
    gap: var(--nv-space-4);
    min-height: 56px;
    padding: var(--nv-space-2) 0;
    border-bottom: 1px solid var(--nv-border);
  }

  .nv-device-row:last-child {
    border-bottom: 0;
  }

  .nv-device-row__main {
    display: flex;
    flex-direction: column;
    flex-grow: 1;
    min-width: 0;
  }

  .nv-device-row__name {
    align-self: flex-start;
    max-width: 100%;
    padding: 0;
    border: 0;
    background: none;
    color: var(--nv-text);
    font: inherit;
    font-weight: 500;
    text-align: left;
    overflow-wrap: anywhere;
    cursor: pointer;
  }

  .nv-device-row__name:hover {
    text-decoration: underline;
  }

  .nv-device-row__status {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
    font-variant-numeric: tabular-nums;
  }

  .nv-device-row__badges {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-2);
  }

  .nv-device-row__actions {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    flex-shrink: 0;
  }

  @media (max-width: 899px) {
    .nv-device-row {
      flex-wrap: wrap;
    }

    .nv-device-row__main {
      flex-basis: 60%;
    }

    .nv-device-row__badges {
      order: 3;
      flex-basis: 100%;
    }

    .nv-device-row__badges:empty {
      display: none;
    }
  }
}
</style>

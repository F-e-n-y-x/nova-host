<script setup>
/**
 * Details and controls for one device: rename, status and times, access, permissions,
 * ending its stream and unpairing (with an inline confirmation step).
 */
import { computed, nextTick, shallowRef, useId, useTemplateRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { Copy } from '@lucide/vue'
import NvDialog from '../components/NvDialog.vue'
import NvButton from '../components/NvButton.vue'
import NvIconButton from '../components/NvIconButton.vue'
import NvTextField from '../components/NvTextField.vue'
import NvSettingRow from '../components/NvSettingRow.vue'
import NvSwitch from '../components/NvSwitch.vue'
import NvAlert from '../components/NvAlert.vue'
import PermissionEditor from './PermissionEditor.vue'
import { absoluteTime, isValidDeviceName, relativeTime } from './format'

const props = defineProps({
  device: { type: Object, default: null },
  busy: { type: Boolean, default: false },
})
const open = defineModel('open', { type: Boolean, default: false })
const emit = defineEmits(['rename', 'permissions', 'toggle', 'disconnect', 'unpair', 'copy-id'])

const { t, locale } = useI18n()
const id = useId()

const draftName = shallowRef('')
const nameError = shallowRef('')
const confirming = shallowRef(false)
const cancelButton = useTemplateRef('cancelUnpair')

const name = computed(() => props.device?.name || t('nova.devices.unnamed'))
const enabled = computed(() => props.device?.enabled !== false)
const nameChanged = computed(() => draftName.value.trim() !== (props.device?.name || ''))
const status = computed(() => {
  if (!props.device) return ''
  if (props.device.connected) return t('nova.devices.streaming_now')
  return enabled.value ? t('nova.devices.status_enabled') : t('nova.devices.status_disabled')
})
const lastConnected = computed(() => (props.device?.last_connected_at
  ? relativeTime(props.device.last_connected_at, locale.value)
  : t('nova.devices.never_connected')))
const lastConnectedTitle = computed(() => absoluteTime(props.device?.last_connected_at, locale.value) || null)
const pairedAt = computed(() => absoluteTime(props.device?.paired_at, locale.value) || t('nova.devices.paired_unknown'))

watch(() => props.device?.uuid, () => {
  draftName.value = props.device?.name || ''
  nameError.value = ''
  confirming.value = false
}, { immediate: true })

/** Validate and send the new name. */
function saveName() {
  const value = draftName.value.trim()
  if (!isValidDeviceName(value)) {
    nameError.value = t('nova.devices.name_invalid')
    return
  }
  nameError.value = ''
  draftName.value = value
  emit('rename', value)
}

/** Show the unpair confirmation and focus its Cancel button. */
async function askUnpair() {
  confirming.value = true
  await nextTick()
  cancelButton.value?.$el?.focus?.()
}
</script>

<template>
  <NvDialog v-model:open="open" size="lg" :title="device ? name : t('nova.devices.title')">
    <div v-if="device" class="nv-panel">
      <form class="nv-panel__rename" @submit.prevent="saveName">
        <div class="nv-grow">
          <NvTextField v-model="draftName" :label="t('nova.devices.name_label')" :error="nameError"
                       :hint="t('nova.devices.name_hint')" autocomplete="off" />
        </div>
        <NvButton type="submit" variant="secondary" :disabled="!nameChanged || busy">{{ t('nova.devices.rename') }}</NvButton>
      </form>

      <dl class="nv-panel__facts">
        <dt>{{ t('nova.devices.fact_status') }}</dt>
        <dd>{{ status }}</dd>
        <dt>{{ t('nova.devices.fact_last_connected') }}</dt>
        <dd :title="lastConnectedTitle">{{ lastConnected }}</dd>
        <dt>{{ t('nova.devices.fact_paired') }}</dt>
        <dd>{{ pairedAt }}</dd>
        <dt>{{ t('nova.devices.id_label') }}</dt>
        <dd class="nv-panel__id">
          <code class="nv-mono">{{ device.uuid }}</code>
          <NvIconButton size="sm" :label="t('nova.devices.copy_id', { name })" @click="emit('copy-id', device.uuid)">
            <Copy :size="16" aria-hidden="true" />
          </NvIconButton>
        </dd>
      </dl>

      <NvAlert v-if="device.connected" variant="success" :title="t('nova.devices.streaming_title', { name })">
        {{ t('nova.devices.streaming_desc') }}
        <template #actions>
          <NvButton size="sm" variant="secondary" :loading="busy" @click="emit('disconnect')">{{ t('nova.devices.disconnect') }}</NvButton>
        </template>
      </NvAlert>

      <NvSettingRow :label="t('nova.devices.allow_label')" :description="t('nova.devices.allow_desc', { name })">
        <template #default="{ labelId, descriptionId }">
          <NvSwitch :model-value="enabled" :labelledby="labelId" :describedby="descriptionId" :disabled="busy" show-state
                    @update:model-value="(value) => emit('toggle', value)" />
        </template>
      </NvSettingRow>

      <PermissionEditor :permissions="device.permissions" :device-name="name" :disabled="busy"
                        @change="(update, next) => emit('permissions', update, next)" />

      <section class="nv-panel__danger" :aria-labelledby="`${id}-danger`">
        <h3 :id="`${id}-danger`" class="nv-panel__danger-title">{{ t('nova.devices.unpair_section') }}</h3>
        <template v-if="!confirming">
          <p class="nv-panel__danger-desc">{{ t('nova.devices.unpair_desc', { name }) }}</p>
          <div><NvButton variant="danger" @click="askUnpair">{{ t('nova.devices.unpair_named', { name }) }}</NvButton></div>
        </template>
        <div v-else class="nv-panel__confirm" role="group" :aria-labelledby="`${id}-confirm`">
          <p :id="`${id}-confirm`" class="nv-panel__confirm-title">{{ t('nova.devices.unpair_confirm_title', { name }) }}</p>
          <p class="nv-panel__danger-desc">{{ t('nova.devices.unpair_confirm_desc') }}</p>
          <div class="nv-row">
            <NvButton ref="cancelUnpair" variant="secondary" @click="confirming = false">{{ t('nova.common.cancel') }}</NvButton>
            <NvButton variant="danger" :loading="busy" @click="emit('unpair')">{{ t('nova.devices.unpair_named', { name }) }}</NvButton>
          </div>
        </div>
      </section>
    </div>
  </NvDialog>
</template>

<style scoped>
@layer components {
  .nv-panel {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-5);
  }

  .nv-panel__rename {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-3);
  }

  .nv-panel__rename > :last-child {
    margin-top: 26px;
  }

  .nv-panel__facts {
    display: grid;
    grid-template-columns: max-content minmax(0, 1fr);
    gap: var(--nv-space-2) var(--nv-space-4);
    margin: 0;
    font-size: var(--nv-text-sm);
  }

  .nv-panel__facts dt {
    color: var(--nv-text-muted);
  }

  .nv-panel__facts dd {
    margin: 0;
    font-variant-numeric: tabular-nums;
    overflow-wrap: anywhere;
  }

  .nv-panel__id {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
  }

  .nv-panel__danger {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    padding-top: var(--nv-space-4);
    border-top: 1px solid var(--nv-border);
  }

  .nv-panel__danger-title,
  .nv-panel__confirm-title {
    margin: 0;
    font-size: var(--nv-text-md);
    font-weight: 600;
  }

  .nv-panel__danger-desc {
    margin: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-panel__confirm {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    padding: var(--nv-space-3);
    border-radius: var(--nv-radius-md);
    background: var(--nv-danger-tint);
  }

  @media (max-width: 599px) {
    .nv-panel__rename {
      flex-direction: column;
      align-items: stretch;
    }

    .nv-panel__rename > :last-child {
      margin-top: 0;
    }
  }
}
</style>

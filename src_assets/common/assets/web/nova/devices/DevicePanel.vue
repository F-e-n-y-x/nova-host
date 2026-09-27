<script setup>
/**
 * Device details side panel (route-addressable as /devices/:uuid). Name, whether it may connect
 * and its permissions are edited as a draft and sent together with Save; ending the stream and
 * unpairing act at once (unpairing behind a confirmation). Closing with unsaved edits asks first.
 */
import { computed, shallowRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import NvSheet from '../components/NvSheet.vue'
import NvButton from '../components/NvButton.vue'
import NvTextField from '../components/NvTextField.vue'
import NvSwitch from '../components/NvSwitch.vue'
import NvConfirmDialog from '../components/NvConfirmDialog.vue'
import PermissionEditor from './PermissionEditor.vue'
import {
  PERMISSION_FLAGS, absoluteTime, isValidDeviceName, permissionFlags, presetForFlags, relativeTime, shortId,
} from './format'
import { sessionSummary } from '../live'

const props = defineProps({
  device: { type: Object, default: null },
  session: { type: Object, default: null },
  busy: { type: Boolean, default: false },
})
const open = defineModel('open', { type: Boolean, default: false })
const emit = defineEmits(['save', 'disconnect', 'unpair', 'copy-id'])

const { t, locale } = useI18n()

const draftName = shallowRef('')
const draftEnabled = shallowRef(true)
const draftFlags = shallowRef(permissionFlags(null))
const nameError = shallowRef('')
const confirmUnpair = shallowRef(false)
const confirmDiscard = shallowRef(false)
let resolveDiscard = null

const name = computed(() => props.device?.name || t('nova.devices.unnamed'))
const live = computed(() => !!(props.session || props.device?.connected))
const savedFlags = computed(() => permissionFlags(props.device?.permissions))

const nameChanged = computed(() => draftName.value.trim() !== (props.device?.name || ''))
const enabledChanged = computed(() => draftEnabled.value !== (props.device?.enabled !== false))
const flagsChanged = computed(() => PERMISSION_FLAGS.some((f) => draftFlags.value[f] !== savedFlags.value[f]))
const dirty = computed(() => nameChanged.value || enabledChanged.value || flagsChanged.value)

const status = computed(() => {
  if (!props.device) return ''
  if (props.session?.app_name) return t('nova.devices.streaming_app', { app: props.session.app_name })
  if (live.value) return t('nova.devices.streaming_now')
  return props.device.enabled !== false ? t('nova.devices.status_enabled') : t('nova.devices.status_disabled')
})
const liveStats = computed(() => sessionSummary(props.session))
const lastConnected = computed(() => {
  if (live.value) return t('nova.devices.now')
  return props.device?.last_connected_at ? relativeTime(props.device.last_connected_at, locale.value) : t('nova.devices.never_connected')
})
const lastConnectedTitle = computed(() => absoluteTime(props.device?.last_connected_at, locale.value) || null)
const pairedAt = computed(() => absoluteTime(props.device?.paired_at, locale.value) || t('nova.devices.paired_unknown'))

/** Reset the draft to what the host has. */
function resetDraft() {
  draftName.value = props.device?.name || ''
  draftEnabled.value = props.device?.enabled !== false
  draftFlags.value = { ...savedFlags.value }
  nameError.value = ''
}

watch(() => props.device?.uuid, resetDraft, { immediate: true })

/** Check the name when the field loses focus. */
function checkName() {
  nameError.value = isValidDeviceName(draftName.value.trim()) ? '' : t('nova.devices.name_invalid')
}

/** Send every changed field in one update. */
function save() {
  checkName()
  if (nameError.value || !dirty.value) return
  const patch = {}
  const next = {}
  if (nameChanged.value) patch.name = draftName.value.trim()
  if (enabledChanged.value) patch.enabled = draftEnabled.value
  if (flagsChanged.value) {
    const preset = presetForFlags(draftFlags.value)
    patch.permissions = preset === 'custom' ? { ...draftFlags.value } : { preset }
    next.permissions = { ...draftFlags.value, preset }
  }
  draftName.value = draftName.value.trim()
  emit('save', patch, next)
}

/**
 * Ask before closing with unsaved edits.
 *
 * @returns {Promise<boolean>} Whether the panel may close.
 */
function beforeClose() {
  if (!dirty.value) return true
  confirmDiscard.value = true
  return new Promise((resolve) => {
    resolveDiscard = resolve
  })
}

/**
 * Answer the discard question.
 *
 * @param {boolean} discard Whether to throw the edits away.
 */
function answerDiscard(discard) {
  confirmDiscard.value = false
  if (discard) resetDraft()
  resolveDiscard?.(discard)
  resolveDiscard = null
}

watch(confirmDiscard, (isOpen) => {
  if (!isOpen && resolveDiscard) answerDiscard(false)
})
</script>

<template>
  <NvSheet v-model:open="open" :title="t('nova.devices.details_title')" :subtitle="device ? name : ''" :before-close="beforeClose">
    <form v-if="device" class="nv-dpanel" novalidate @submit.prevent="save">
      <NvTextField v-model="draftName" :label="t('nova.devices.name_label')" :hint="t('nova.devices.name_hint')"
                   :error="nameError" autocomplete="off" spellcheck="false" @blur="checkName" />

      <dl class="nv-dpanel__facts">
        <dt>{{ t('nova.devices.fact_status') }}</dt>
        <dd :class="{ 'nv-dpanel__live': live }">
          {{ status }}
          <span v-if="liveStats" class="nv-dpanel__stats nv-mono">{{ liveStats }}</span>
        </dd>
        <dt>{{ t('nova.devices.fact_paired') }}</dt>
        <dd>{{ pairedAt }}</dd>
        <dt>{{ t('nova.devices.fact_last_connected') }}</dt>
        <dd :title="lastConnectedTitle">{{ lastConnected }}</dd>
        <dt>{{ t('nova.devices.id_label') }}</dt>
        <dd class="nv-dpanel__id">
          <code class="nv-mono" :title="device.uuid">{{ shortId(device.uuid) }}</code>
          <button type="button" class="nv-dpanel__copy" :aria-label="t('nova.devices.copy_id', { name })"
                  @click="emit('copy-id', device.uuid)">{{ t('nova.devices.copy') }}</button>
        </dd>
      </dl>

      <section class="nv-dpanel__section" aria-labelledby="nv-dpanel-access">
        <h3 id="nv-dpanel-access" class="nv-dpanel__heading">{{ t('nova.devices.access_title') }}</h3>
        <div class="nv-dpanel__allow">
          <span class="nv-dpanel__allow-text">
            <span id="nv-dpanel-allow" class="nv-dpanel__allow-label">{{ t('nova.devices.allow_label') }}</span>
            <span id="nv-dpanel-allow-desc" class="nv-dpanel__allow-desc">{{ t('nova.devices.allow_desc', { name }) }}</span>
          </span>
          <NvSwitch v-model="draftEnabled" labelledby="nv-dpanel-allow" describedby="nv-dpanel-allow-desc" :disabled="busy" />
        </div>
        <PermissionEditor v-model="draftFlags" :disabled="busy" />
        <p class="nv-dpanel__note">{{ t('nova.devices.permissions_intro', { name }) }}</p>
      </section>
      <button type="submit" hidden tabindex="-1" aria-hidden="true"></button>
    </form>

    <template #footer-start>
      <NvButton v-if="device && live" variant="secondary" :loading="busy" @click="emit('disconnect')">{{ t('nova.devices.disconnect') }}</NvButton>
      <NvButton v-if="device" variant="danger" @click="confirmUnpair = true">{{ t('nova.devices.unpair_ellipsis') }}</NvButton>
    </template>
    <template #footer>
      <NvButton variant="primary" :disabled="!dirty || busy" :loading="busy" @click="save">{{ t('nova.devices.save') }}</NvButton>
    </template>
  </NvSheet>

  <NvConfirmDialog v-model:open="confirmUnpair" :title="t('nova.devices.unpair_confirm_title', { name })"
                   :description="t('nova.devices.unpair_confirm_desc')" :confirm-label="t('nova.devices.unpair')"
                   :loading="busy" @confirm="emit('unpair'); confirmUnpair = false" />
  <NvConfirmDialog v-model:open="confirmDiscard" :title="t('nova.devices.discard_title', { name })"
                   :description="t('nova.devices.discard_desc')" :confirm-label="t('nova.devices.discard')"
                   @confirm="answerDiscard(true)" />
</template>

<style scoped>
@layer components {
  .nv-dpanel {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-5);
  }

  .nv-dpanel__facts {
    display: grid;
    grid-template-columns: 110px minmax(0, 1fr);
    gap: var(--nv-space-2) var(--nv-space-3);
    margin: 0;
    font-size: var(--nv-text-sm);
  }

  .nv-dpanel__facts dt {
    color: var(--nv-text-muted);
    font-weight: 400;
  }

  .nv-dpanel__facts dd {
    margin: 0;
    color: var(--nv-text);
    font-variant-numeric: tabular-nums;
    overflow-wrap: anywhere;
  }

  .nv-dpanel__facts .nv-dpanel__live {
    color: var(--nv-success);
  }

  .nv-dpanel__stats {
    display: block;
    margin-top: 2px;
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
  }

  .nv-dpanel__id {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
  }

  .nv-dpanel__copy {
    min-height: 24px;
    padding: 0 var(--nv-space-1);
    border: 0;
    border-radius: var(--nv-radius-sm);
    background: none;
    color: var(--nv-accent-text);
    font: inherit;
    font-size: var(--nv-text-xs);
    cursor: pointer;
  }

  .nv-dpanel__copy:hover {
    text-decoration: underline;
  }

  .nv-dpanel__section {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
  }

  .nv-dpanel__heading {
    margin: 0;
    font-size: var(--nv-text-sm);
    font-weight: 600;
  }

  .nv-dpanel__allow {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    padding: var(--nv-space-3) var(--nv-space-3) var(--nv-space-3) var(--nv-space-4);
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-surface);
  }

  .nv-dpanel__allow-text {
    display: flex;
    flex-direction: column;
    flex-grow: 1;
    min-width: 0;
  }

  .nv-dpanel__allow-desc,
  .nv-dpanel__note {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
  }

  .nv-dpanel__note {
    margin: 0;
  }
}
</style>

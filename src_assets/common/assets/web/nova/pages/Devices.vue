<script setup>
/**
 * Devices page (A+C design): paired devices in a table, a details side panel addressable as
 * /devices/:uuid, the pending-pairing notice, and the unpair-all danger zone at the bottom.
 * Live sessions come from the shared live store, matched to devices by client_uuid.
 */
import { computed, shallowRef, useTemplateRef } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { Plus, Smartphone } from '@lucide/vue'
import NvPage from '../components/NvPage.vue'
import NvButton from '../components/NvButton.vue'
import NvAlert from '../components/NvAlert.vue'
import NvEmptyState from '../components/NvEmptyState.vue'
import NvSkeleton from '../components/NvSkeleton.vue'
import NvConfirmDialog from '../components/NvConfirmDialog.vue'
import DeviceTable from '../devices/DeviceTable.vue'
import DevicePanel from '../devices/DevicePanel.vue'
import UnpairAllSection from '../devices/UnpairAllSection.vue'
import { useDevices } from '../devices/useDevices'
import { usePendingPairings } from '../devices/usePendingPairings'
import { sessionFor } from '../devices/format'
import { useLiveSession } from '../live'
import { toast } from '../toast'

const { t } = useI18n()
const route = useRoute()
const router = useRouter()

const store = useDevices()
const live = useLiveSession()
const { requests: pending } = usePendingPairings({ interval: 5000 })
const unpairAllSection = useTemplateRef('unpairAllPanel')
const unpairingAll = shallowRef(false)
const unpairTarget = shallowRef('')

const devices = store.devices
const sessions = computed(() => live.sessions.value || [])
const selectedUuid = computed(() => (typeof route.params.uuid === 'string' ? route.params.uuid : ''))
const selected = computed(() => (selectedUuid.value ? store.byUuid(selectedUuid.value) : null))
const selectedSession = computed(() => (selected.value ? sessionFor(sessions.value, selected.value.uuid) : null))
const panelOpen = computed({
  get: () => !!selected.value,
  set: (value) => {
    if (!value) closePanel()
  },
})
const query = computed({
  get: () => (typeof route.query.q === 'string' ? route.query.q : ''),
  set: (q) => router.replace({ query: { ...route.query, q: q || undefined } }),
})
const pendingNames = computed(() => pending.value.map((p) => p.suggested_name || p.name || t('nova.pair.unknown_device')).join(', '))
const firstLoad = computed(() => store.loading.value && !devices.value.length && !store.error.value)
const confirmRowUnpair = computed({
  get: () => !!unpairTarget.value,
  set: (value) => {
    if (!value) unpairTarget.value = ''
  },
})

/**
 * Name to use in messages about a device.
 *
 * @param {string} uuid Device ID.
 * @returns {string} Its name or a fallback.
 */
function nameOf(uuid) {
  return store.byUuid(uuid)?.name || t('nova.devices.unnamed')
}

/**
 * Open a device's panel.
 *
 * @param {string} uuid Device ID.
 */
function openPanel(uuid) {
  router.push({ path: `/devices/${encodeURIComponent(uuid)}`, query: route.query })
}

/** Close the panel, going back to the list. */
function closePanel() {
  router.push({ path: '/devices', query: route.query })
}

/**
 * Run an action and report the outcome.
 *
 * @param {() => Promise<void>} action The action.
 * @param {string} success Toast shown when it works (optional).
 * @param {string} failure Toast shown when it fails; the host's reason is appended.
 * @param {object} [successOptions] Extra toast options (e.g. an Undo action).
 * @returns {Promise<boolean>} Whether it worked.
 */
async function report(action, success, failure, successOptions) {
  try {
    await action()
    if (success) toast.success(success, successOptions)
    return true
  } catch (e) {
    toast.danger(e?.message ? `${failure} ${e.message}` : failure)
    return false
  }
}

/**
 * Allow or block a device (from the row menu), with Undo.
 *
 * @param {string} uuid Device ID.
 * @param {boolean} enabled New state.
 */
function toggle(uuid, enabled) {
  const name = nameOf(uuid)
  report(
    () => store.setEnabled(uuid, enabled),
    enabled ? t('nova.devices.allowed_toast', { name }) : t('nova.devices.blocked_toast', { name }),
    t('nova.devices.update_failed', { name }),
    { action: { label: t('nova.common.undo'), onClick: () => toggle(uuid, !enabled) } },
  )
}

/**
 * Save the panel's draft.
 *
 * @param {object} patch Changed fields to send.
 * @param {object} next Fields to show while saving.
 */
function save(patch, next) {
  const uuid = selectedUuid.value
  const name = patch.name || nameOf(uuid)
  report(() => store.save(uuid, patch, next), t('nova.devices.saved_toast', { name }), t('nova.devices.save_failed', { name: nameOf(uuid) }))
}

/**
 * End a device's stream.
 *
 * @param {string} uuid Device ID.
 */
async function disconnect(uuid) {
  const name = nameOf(uuid)
  const ok = await report(() => store.disconnect(uuid), t('nova.devices.disconnected_toast', { name }), t('nova.devices.disconnect_failed', { name }))
  if (ok) live.refresh()
}

/**
 * Unpair a device and close its panel if it was open.
 *
 * @param {string} uuid Device ID.
 */
async function unpair(uuid) {
  const name = nameOf(uuid)
  const ok = await report(() => store.unpair(uuid), t('nova.devices.unpaired_toast', { name }), t('nova.devices.unpair_failed', { name }))
  unpairTarget.value = ''
  if (ok && selectedUuid.value === uuid) closePanel()
}

/** Unpair every device. */
async function unpairAll() {
  unpairingAll.value = true
  const ok = await report(() => store.unpairAll(), t('nova.devices.unpaired_all_toast'), t('nova.devices.unpair_all_failed'))
  unpairingAll.value = false
  if (ok) {
    unpairAllSection.value?.close()
    if (selectedUuid.value) closePanel()
  }
}

/**
 * Copy a device ID.
 *
 * @param {string} uuid Device ID.
 */
async function copyId(uuid) {
  try {
    await navigator.clipboard.writeText(uuid)
    toast.success(t('nova.devices.copied', { name: nameOf(uuid) }))
  } catch {
    toast.danger(t('nova.devices.copy_failed'))
  }
}
</script>

<template>
  <NvPage :title="t('nova.devices.title')">
    <template #actions>
      <NvButton variant="primary" to="/pair" :icon="Plus">{{ t('nova.devices.pair_device') }}</NvButton>
    </template>

    <div class="nv-devices">
      <NvAlert v-if="pending.length" variant="info" live :title="t('nova.devices.pending_title', pending.length)">
        {{ t('nova.devices.pending_desc', { names: pendingNames }, pending.length) }}
        <template #actions><NvButton size="sm" variant="primary" to="/pair">{{ t('nova.devices.enter_pin') }}</NvButton></template>
      </NvAlert>

      <div v-if="firstLoad" class="nv-devices__loading" aria-busy="true">
        <span class="nv-visually-hidden" role="status">{{ t('nova.devices.loading') }}</span>
        <NvSkeleton :lines="3" height="44px" />
      </div>
      <NvAlert v-else-if="store.error.value && !devices.length" variant="danger" :title="t('nova.devices.load_failed')">
        {{ store.error.value.message }}
        <template #actions><NvButton size="sm" variant="secondary" @click="store.reload()">{{ t('nova.common.retry') }}</NvButton></template>
      </NvAlert>
      <NvEmptyState v-else-if="!devices.length" :title="t('nova.devices.empty_title')" :description="t('nova.devices.empty_desc')">
        <template #icon><Smartphone :size="32" /></template>
        <template #actions><NvButton variant="primary" to="/pair">{{ t('nova.devices.pair_device') }}</NvButton></template>
      </NvEmptyState>
      <DeviceTable v-else v-model:query="query" :devices="devices" :sessions="sessions" :selected="selectedUuid"
                   :busy="store.busy.value" @open="openPanel" @toggle="toggle" @copy-id="copyId"
                   @disconnect="disconnect" @unpair="(uuid) => (unpairTarget = uuid)" />

      <div v-if="devices.length" class="nv-devices__danger">
        <UnpairAllSection ref="unpairAllPanel" :count="devices.length" :busy="unpairingAll" @confirm="unpairAll" />
      </div>
    </div>

    <DevicePanel v-model:open="panelOpen" :device="selected" :session="selectedSession"
                 :busy="!!store.busy.value[selectedUuid]" @save="save" @disconnect="disconnect(selectedUuid)"
                 @unpair="unpair(selectedUuid)" @copy-id="copyId" />

    <NvConfirmDialog v-model:open="confirmRowUnpair" :title="t('nova.devices.unpair_confirm_title', { name: nameOf(unpairTarget) })"
                     :description="t('nova.devices.unpair_confirm_desc')" :confirm-label="t('nova.devices.unpair')"
                     :loading="!!store.busy.value[unpairTarget]" @confirm="unpair(unpairTarget)" />
  </NvPage>
</template>

<style scoped>
@layer components {
  .nv-devices {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
    min-height: calc(100vh - var(--nv-topbar-height) - var(--nv-space-6) - var(--nv-space-7));
  }

  .nv-devices__danger {
    margin-top: auto;
  }
}
</style>

<script setup>
/**
 * Devices page: composes the paired-device list, the per-device panel (addressable as
 * /devices/:uuid), the pending-pairing notice and the unpair-all danger zone.
 */
import { computed, shallowRef, useTemplateRef } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { Smartphone } from '@lucide/vue'
import NvPage from '../components/NvPage.vue'
import NvCard from '../components/NvCard.vue'
import NvButton from '../components/NvButton.vue'
import NvAlert from '../components/NvAlert.vue'
import NvEmptyState from '../components/NvEmptyState.vue'
import NvSkeleton from '../components/NvSkeleton.vue'
import DeviceList from '../devices/DeviceList.vue'
import DevicePanel from '../devices/DevicePanel.vue'
import UnpairAllSection from '../devices/UnpairAllSection.vue'
import { useDevices } from '../devices/useDevices'
import { usePendingPairings } from '../devices/usePendingPairings'
import { toast } from '../toast'

const { t } = useI18n()
const route = useRoute()
const router = useRouter()

const store = useDevices()
const { requests: pending } = usePendingPairings({ interval: 5000 })
const unpairAllSection = useTemplateRef('unpairAllPanel')
const unpairingAll = shallowRef(false)

const devices = store.devices
const selectedUuid = computed(() => (typeof route.params.uuid === 'string' ? route.params.uuid : ''))
const selected = computed(() => (selectedUuid.value ? store.byUuid(selectedUuid.value) : null))
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
const pendingNames = computed(() => pending.value.map((p) => p.name || t('nova.pair.unknown_device')).join(', '))
const firstLoad = computed(() => store.loading.value && !devices.value.length && !store.error.value)

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
 * @returns {Promise<boolean>} Whether it worked.
 */
async function report(action, success, failure) {
  try {
    await action()
    if (success) toast.success(success)
    return true
  } catch (e) {
    toast.danger(e?.message ? `${failure} ${e.message}` : failure)
    return false
  }
}

/**
 * Allow or block a device.
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
  )
}

/**
 * Rename the selected device.
 *
 * @param {string} name New name.
 */
function rename(name) {
  const uuid = selectedUuid.value
  const before = nameOf(uuid)
  report(() => store.rename(uuid, name), t('nova.devices.renamed_toast', { before, name }), t('nova.devices.rename_failed', { name: before }))
}

/**
 * Change the selected device's permissions.
 *
 * @param {object} update What to send.
 * @param {object} next What to show meanwhile.
 */
function setPermissions(update, next) {
  const uuid = selectedUuid.value
  const name = nameOf(uuid)
  report(() => store.setPermissions(uuid, update, next), '', t('nova.devices.permissions_failed', { name }))
}

/** End the selected device's stream. */
function disconnect() {
  const uuid = selectedUuid.value
  const name = nameOf(uuid)
  report(() => store.disconnect(uuid), t('nova.devices.disconnected_toast', { name }), t('nova.devices.disconnect_failed', { name }))
}

/** Unpair the selected device and close its panel. */
async function unpair() {
  const uuid = selectedUuid.value
  const name = nameOf(uuid)
  const ok = await report(() => store.unpair(uuid), t('nova.devices.unpaired_toast', { name }), t('nova.devices.unpair_failed', { name }))
  if (ok) closePanel()
}

/** Unpair every device. */
async function unpairAll() {
  unpairingAll.value = true
  const ok = await report(() => store.unpairAll(), t('nova.devices.unpaired_all_toast'), t('nova.devices.unpair_all_failed'))
  unpairingAll.value = false
  if (ok) unpairAllSection.value?.close()
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
    <template #subtitle><span>{{ t('nova.devices.intro') }}</span></template>
    <template #actions><NvButton variant="primary" to="/pair">{{ t('nova.nav.pair') }}</NvButton></template>

    <div class="nv-stack">
      <NvAlert v-if="pending.length" variant="info" live :title="t('nova.devices.pending_title', pending.length)">
        {{ t('nova.devices.pending_desc', { names: pendingNames }, pending.length) }}
        <template #actions><NvButton size="sm" variant="primary" to="/pair">{{ t('nova.devices.enter_pin') }}</NvButton></template>
      </NvAlert>

      <NvCard :title="t('nova.devices.paired_title')">
        <template v-if="devices.length" #actions>
          <span class="nv-muted nv-devices__count">{{ t('nova.devices.count', { n: devices.length }, devices.length) }}</span>
        </template>

        <div v-if="firstLoad" aria-busy="true">
          <span class="nv-visually-hidden" role="status">{{ t('nova.common.loading') }}</span>
          <NvSkeleton :lines="4" height="20px" />
        </div>
        <NvAlert v-else-if="store.error.value && !devices.length" variant="danger" :title="t('nova.devices.load_failed')">
          {{ store.error.value.message }}
          <template #actions><NvButton size="sm" variant="secondary" @click="store.reload()">{{ t('nova.common.retry') }}</NvButton></template>
        </NvAlert>
        <NvEmptyState v-else-if="!devices.length" :title="t('nova.devices.empty_title')" :description="t('nova.devices.empty_desc')">
          <template #icon><Smartphone :size="32" /></template>
          <template #actions><NvButton variant="primary" to="/pair">{{ t('nova.nav.pair') }}</NvButton></template>
        </NvEmptyState>
        <DeviceList v-else v-model:query="query" :devices="devices" :busy="store.busy.value"
                    @open="openPanel" @toggle="toggle" />
      </NvCard>

      <UnpairAllSection v-if="devices.length" ref="unpairAllPanel" :count="devices.length" :busy="unpairingAll" @confirm="unpairAll" />
    </div>

    <DevicePanel v-model:open="panelOpen" :device="selected" :busy="!!store.busy.value[selectedUuid]"
                 @rename="rename" @permissions="setPermissions" @toggle="(value) => toggle(selectedUuid, value)"
                 @disconnect="disconnect" @unpair="unpair" @copy-id="copyId" />
  </NvPage>
</template>

<style scoped>
@layer components {
  .nv-devices__count {
    font-size: var(--nv-text-sm);
    font-variant-numeric: tabular-nums;
  }
}
</style>

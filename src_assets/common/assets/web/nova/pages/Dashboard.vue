<script setup>
/**
 * Overview (Final_Dashboard / Final_Dashboard_Live): status band, "Needs attention", the live stream
 * bar while streaming, desktop preview + stream health, then Library, Devices and Hardware.
 * Newer host APIs are optional; missing ones hide their widget or fall back to the log.
 */
import { computed, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { Cpu, Monitor, Radio, ShieldCheck, Smartphone, Plus } from '@lucide/vue'
import NvPage from '../components/NvPage.vue'
import NvButton from '../components/NvButton.vue'
import NvAlert from '../components/NvAlert.vue'
import NvStatBand from '../components/NvStatBand.vue'
import NvAttention from '../components/NvAttention.vue'
import NvConfirmDialog from '../components/NvConfirmDialog.vue'
import { postJson } from '../api'
import { useUpdateStatus } from '../update'
import { useLiveSession } from '../live'
import { toast } from '../toast'
import { useOverview } from './overview/useOverview'
import { previewState } from './overview/usePreview'
import { attentionFromHealth, captureLabel, encoderLabel, shortVersion } from './overview/format'
import StreamBar from './overview/StreamBar.vue'
import UpdateBanner from './overview/UpdateBanner.vue'
import PreviewCard from './overview/PreviewCard.vue'
import StreamHealthCard from './overview/StreamHealthCard.vue'
import LibraryCard from './overview/LibraryCard.vue'
import DevicesCard from './overview/DevicesCard.vue'
import HardwareCard from './overview/HardwareCard.vue'

const { t } = useI18n()
const o = useOverview()
const live = useLiveSession()

watch(o.needLogs, (need) => { if (need) o.ensureLogs() }, { immediate: true })

const unreachable = computed(() => Boolean(o.config.error.value))
const session = computed(() => live.current.value)
const streaming = computed(() => live.active.value)
const streamingUuids = computed(() => new Set(live.sessions.value.map((s) => s.client_uuid).filter(Boolean)))

// The host checks the (private) release repository; the browser never calls GitHub.
const updates = useUpdateStatus()

const version = computed(() => o.hostInfo.data.value?.version || o.config.data.value?.version || '')
// Tag of a real update, '' when the check succeeded with nothing newer, null when unknown (no check, error).
const update = computed(() => {
  const s = updates.status.value
  if (!s?.enabled || s.error || !s.checked_at) return null
  return updates.available.value ? s.latest.tag : ''
})

const updateHint = computed(() => {
  const error = updates.status.value?.error
  if (error === 'private' || error === 'not_found' || error === 'unauthorized' || error === 'bad_token') return t('nova.update.needs_token')
  return ''
})

const encoderCell = computed(() => {
  const enc = o.hostInfo.data.value?.encoders
  if (enc?.active) return { value: encoderLabel(enc.active), sub: (enc.codecs || []).join(' · ') }
  const hw = o.logEncoders.value.filter((e) => e.hardware)
  const list = hw.length ? hw : o.logEncoders.value
  if (!list.length) return { value: '—', sub: t('nova.overview.none_detected') }
  return { value: [...new Set(list.map((e) => e.label))].join(', '), sub: list.map((e) => e.codec).join(' · ') }
})

const captureCell = computed(() => {
  const cap = o.hostInfo.data.value?.capture
  const cfg = o.config.data.value?.capture
  const method = cap?.method || cfg
  const display = o.displays.data.value?.find((d) => d.configured) || o.displays.data.value?.find((d) => d.primary)
  const bits = []
  if (cap?.zero_copy) bits.push(t('nova.overview.zero_copy'))
  if (display?.refresh_hz) bits.push(`${Math.round(display.refresh_hz)} Hz`)
  return { value: method ? captureLabel(method) : t('nova.overview.auto'), sub: bits.join(' · ') }
})

const cells = computed(() => {
  const devices = o.clients.data.value || []
  const enabled = devices.filter((d) => d.enabled !== false).length
  const loading = o.config.loading.value
  return [
    { key: 'stream', label: t('nova.overview.stream'), icon: Radio, status: streaming.value ? 'success' : 'success',
      value: streaming.value ? t('nova.overview.streaming') : t('nova.overview.ready'),
      sub: streaming.value ? t('nova.overview.devices_connected', { n: live.sessions.value.length }, live.sessions.value.length) : t('nova.overview.waiting'), loading },
    { key: 'encoder', label: t('nova.overview.encoder'), icon: Cpu, ...encoderCell.value, to: '/settings#encoder',
      loading: o.hostInfo.loading.value || (o.needLogs.value && o.logs.loading.value) },
    { key: 'capture', label: t('nova.overview.capture'), icon: Monitor, ...captureCell.value, loading: o.hostInfo.loading.value },
    { key: 'devices', label: t('nova.overview.devices_title'), icon: Smartphone, to: '/devices', loading: o.clients.loading.value,
      value: t('nova.overview.paired', { n: devices.length }, devices.length),
      sub: streaming.value ? t('nova.overview.streaming_count', { n: streamingUuids.value.size }) : (enabled === devices.length ? t('nova.overview.all_allowed') : t('nova.overview.allowed_count', { n: enabled })) },
    { key: 'nova', label: 'Nova', icon: ShieldCheck, value: shortVersion(version.value) || '—', loading,
      sub: update.value ? t('nova.overview.update_to', { version: update.value }) : (update.value === '' ? t('nova.overview.up_to_date') : updateHint.value),
      to: updateHint.value ? '/settings#updates' : undefined },
  ]
})

const issues = computed(() => {
  if (o.health.data.value) return attentionFromHealth(o.health.data.value, t)
  return o.logHealth.value.filter((c) => c.status !== 'success').map((c) => ({
    id: c.id, title: t(c.title, c.params || {}), detail: t(c.desc, c.params || {}),
    severity: c.status === 'danger' ? 'danger' : 'warning',
    action: c.to ? { label: c.to.startsWith('/logs') ? t('nova.common.view_logs') : t('nova.overview.open_setting'), to: c.to } : undefined,
  }))
})

const endTarget = ref(null)
const ending = ref(false)
const endError = ref('')
const endOpen = computed({ get: () => Boolean(endTarget.value), set: (v) => { if (!v) endTarget.value = null } })
async function endStream() {
  ending.value = true
  endError.value = ''
  try {
    await postJson('./api/clients/disconnect', { uuid: endTarget.value.client_uuid })
    toast.success(t('nova.overview.stream_ended', { device: endTarget.value.client_name || t('nova.live.unknown_device') }))
    endTarget.value = null
    live.refresh()
  } catch (e) {
    endError.value = e?.status === 404 ? t('nova.overview.end_not_supported') : t('nova.overview.end_failed')
  } finally {
    ending.value = false
  }
}

const closeTarget = ref(null)
const closing = ref(false)
const closeOpen = computed({ get: () => Boolean(closeTarget.value), set: (v) => { if (!v) closeTarget.value = null } })
async function closeApp() {
  closing.value = true
  try {
    await postJson('./api/apps/close')
    toast.success(t('nova.overview.app_closed', { name: closeTarget.value.name }))
    closeTarget.value = null
    o.apps.reload()
  } catch {
    toast.danger(t('nova.overview.close_failed'))
  } finally {
    closing.value = false
  }
}

const detailsOpen = ref(false)
function showDetails() {
  detailsOpen.value = true
  toast.info(t('nova.overview.details_soon'))
}
</script>

<template>
  <NvPage :title="t('nova.overview.title')" grid>
    <template #actions>
      <NvButton variant="primary" :icon="Plus" to="/pair">{{ t('nova.overview.pair_device') }}</NvButton>
    </template>

    <NvAlert v-if="unreachable" variant="danger" :title="t('nova.dashboard.unreachable_title')" live class="nv-ov__full">
      {{ t('nova.dashboard.unreachable_desc') }}
      <template #actions><NvButton size="sm" @click="o.reloadAll()">{{ t('nova.common.retry') }}</NvButton></template>
    </NvAlert>

    <template v-else>
      <NvStatBand :cells="cells" :label="t('nova.overview.status_label')" class="nv-ov__full" />
      <UpdateBanner :status="updates.status.value" :streaming="streaming" :starting="updates.starting.value"
                    :install-error="updates.installError.value" :install="updates.install" class="nv-ov__full" />
      <StreamBar v-if="streaming && session" :session="session" :thumbnail="previewState.url" :more="live.sessions.value.length - 1"
                 class="nv-ov__full" @end="(s) => (endTarget = s)" />
      <NvAttention v-if="issues.length" :issues="issues" class="nv-ov__full" />

      <PreviewCard v-if="o.displays.data.value !== null || previewState.available !== false" :live="streaming"
                   :device="session?.client_name || ''" :displays="o.displays.data.value" class="nv-ov__c7" @details="showDetails" />
      <StreamHealthCard v-if="live.state.available !== false || o.history.data.value !== null" :session="session"
                        :history="o.history.data.value" :loading="o.history.loading.value && live.state.available === null" class="nv-ov__c5" />

      <LibraryCard :data="o.apps.data.value" :loading="o.apps.loading.value" :error="o.apps.error.value" class="nv-ov__c7"
                   @retry="o.apps.reload()" @close="(a) => (closeTarget = a)" />
      <div class="nv-ov__c5 nv-ov__stack">
        <DevicesCard :devices="o.clients.data.value" :loading="o.clients.loading.value" :error="o.clients.error.value"
                     :streaming-uuids="streamingUuids" @retry="o.clients.reload()" />
        <HardwareCard :info="o.hostInfo.data.value" :log-encoders="o.logEncoders.value" :loading="o.hostInfo.loading.value" />
      </div>
    </template>

    <NvConfirmDialog v-model:open="endOpen"
                     :title="t('nova.overview.end_title', { device: endTarget?.client_name || t('nova.live.unknown_device') })"
                     :description="t('nova.overview.end_desc')" :confirm-label="t('nova.overview.end_stream')"
                     :loading="ending" :error="endError" @confirm="endStream" />
    <NvConfirmDialog v-model:open="closeOpen" :title="t('nova.overview.close_title', { name: closeTarget?.name || '' })"
                     :description="t('nova.overview.close_desc')" :confirm-label="t('nova.overview.close_app')"
                     :loading="closing" @confirm="closeApp" />
  </NvPage>
</template>

<style>
@layer components {
  .nv-ov__full { grid-column: 1 / -1; }
  .nv-ov__c7 { grid-column: span 7; }
  .nv-ov__c5 { grid-column: span 5; }
  .nv-ov__stack {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
    min-width: 0;
  }
  .nv-ov__stack > .nv-hw { flex-grow: 1; }

  @media (max-width: 1279px) {
    .nv-ov__c7, .nv-ov__c5 { grid-column: 1 / -1; }
  }
}
</style>

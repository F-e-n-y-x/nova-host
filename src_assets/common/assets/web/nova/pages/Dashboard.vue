<script setup>
/**
 * Dashboard: host status, health derived from the log, paired devices and applications.
 * Uses only existing host APIs; the live session panel waits for a sessions API.
 */
import { computed, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { CircleAlert, CircleCheck, RadioTower, TriangleAlert } from '@lucide/vue'
import NvPage from '../components/NvPage.vue'
import NvCard from '../components/NvCard.vue'
import NvButton from '../components/NvButton.vue'
import NvDataList from '../components/NvDataList.vue'
import NvStatusDot from '../components/NvStatusDot.vue'
import NvBadge from '../components/NvBadge.vue'
import NvEmptyState from '../components/NvEmptyState.vue'
import NvSkeleton from '../components/NvSkeleton.vue'
import NvAlert from '../components/NvAlert.vue'
import NvDialog from '../components/NvDialog.vue'
import { checkForUpdates, fetchJson, getConfig, postJson, useAsync } from '../api'
import { apiFetch } from '../../fetch_utils'
import { detectEncoders, healthChecks, parseLogs } from '../logs'
import { toast } from '../toast'
import SunshineVersion from '../../sunshine_version'

const { t } = useI18n()
const PREVIEW = 5

const config = useAsync(() => getConfig())
const logs = useAsync(async () => {
  const response = await apiFetch('./api/logs')
  if (!response.ok) throw new Error(String(response.status))
  return parseLogs(await response.text())
})
const clients = useAsync(async () => (await fetchJson('./api/clients/list')).named_certs || [])
const apps = useAsync(async () => (await fetchJson('./api/apps')).apps || [])

const release = ref({ loading: true, result: null })

watch(() => config.data.value, async (cfg) => {
  if (!cfg) return
  const pre = cfg.notify_pre_releases === true || cfg.notify_pre_releases === 'true' || cfg.notify_pre_releases === 'enabled'
  release.value = { loading: false, result: await checkForUpdates(pre) }
}, { immediate: true })

const hostName = computed(() => config.data.value?.sunshine_name || globalThis.location?.hostname || '')
const version = computed(() => config.data.value?.version || '')

const updateStatus = computed(() => {
  const current = version.value
  const { loading, result } = release.value
  // Unknown (no releases published, private repo, offline): say nothing.
  if (!current || loading || !result) return null
  const mine = new SunshineVersion(null, current)
  const candidates = [result.prerelease, result.latest].filter(Boolean).map((r) => ({ release: r, version: new SunshineVersion(r, null) }))
  const newer = candidates.find((c) => c.version.isGreater(mine))
  if (newer) {
    const key = newer.release.prerelease ? 'nova.dashboard.prerelease_available' : 'nova.dashboard.update_available'
    return { id: 'update', status: 'warning', title: t(key, { version: newer.version.versionTag }),
      desc: t('nova.dashboard.update_available_desc', { current }), href: newer.release.html_url }
  }
  if (result.latest && mine.isGreater(new SunshineVersion(result.latest, null))) {
    return { id: 'update', status: 'success', title: t('nova.dashboard.dev_build'), desc: t('nova.dashboard.dev_build_desc', { version: current }) }
  }
  if (!result.latest) return null
  return { id: 'update', status: 'success', title: t('nova.dashboard.up_to_date'), desc: t('nova.dashboard.up_to_date_desc', { version: current }) }
})

const health = computed(() => {
  const items = healthChecks(logs.data.value || [], config.data.value || {}).map((c) => ({
    ...c,
    title: t(c.title, c.params || {}),
    desc: t(c.desc, c.params || {}),
    linkLabel: c.to?.startsWith('/logs') ? t('nova.common.view_logs') : t('nova.nav.settings'),
  }))
  if (updateStatus.value) items.push(updateStatus.value)
  const order = { danger: 0, warning: 1, success: 2 }
  return items.sort((a, b) => order[a.status] - order[b.status])
})

const encoders = computed(() => detectEncoders(logs.data.value || []))

const PLATFORM_LABELS = { linux: 'Linux', windows: 'Windows', macos: 'macOS', freebsd: 'FreeBSD' }
const PACING_LABELS = { auto: 'Auto', vblank: 'Vblank', timer: 'Timer' }
const CAPTURE_LABELS = { kms: 'KMS', x11: 'X11', wlr: 'wlroots', portal: 'Portal', nvfbc: 'NvFBC', ddx: 'DXGI', wgc: 'WGC' }

function configValue(key, fallback) {
  const value = config.data.value?.[key]
  return value === undefined || value === '' ? fallback : value
}

const hostItems = computed(() => {
  const hardware = encoders.value.filter((e) => e.hardware)
  const chosen = hardware.length ? hardware : encoders.value
  const mic = configValue('mic_enabled', true)
  const micOn = !['disabled', 'false', false].includes(mic)
  return [
    { term: t('nova.dashboard.host_name'), value: hostName.value },
    { term: t('nova.dashboard.platform'), value: PLATFORM_LABELS[configValue('platform', '')] || configValue('platform', '—') },
    { term: t('nova.dashboard.encoder'), value: chosen.length ? [...new Set(chosen.map((e) => e.label))].join(', ') : t('nova.dashboard.not_detected'), muted: !chosen.length },
    { term: t('nova.dashboard.codecs'), value: chosen.length ? chosen.map((e) => e.codec).join(', ') : '—', muted: !chosen.length },
    { term: t('nova.dashboard.capture'), value: CAPTURE_LABELS[configValue('capture', '')] || configValue('capture', t('nova.dashboard.auto')) },
    { term: t('nova.dashboard.capture_pacing'), value: PACING_LABELS[configValue('capture_pacing', 'auto')] || configValue('capture_pacing', 'auto') },
    { term: t('nova.dashboard.microphone'), value: micOn ? t('nova.common.enabled') : t('nova.common.disabled') },
  ]
})

const healthIcons = { success: CircleCheck, warning: TriangleAlert, danger: CircleAlert }

const restartOpen = ref(false)
const restarting = ref(false)

async function restart() {
  restarting.value = true
  try {
    await apiFetch('./api/restart', { method: 'POST', headers: { 'Content-Type': 'application/json' } })
    toast.info(t('nova.dashboard.restarting'))
  } catch {
    // The connection drops while the host restarts; that is expected.
    toast.info(t('nova.dashboard.restarting'))
  } finally {
    restarting.value = false
    restartOpen.value = false
  }
}

const closing = ref(false)
async function closeApp() {
  closing.value = true
  try {
    await postJson('./api/apps/close')
    toast.success(t('nova.dashboard.close_app_done'))
  } catch {
    toast.danger(t('nova.dashboard.close_app_failed'))
  } finally {
    closing.value = false
  }
}
</script>

<template>
  <NvPage :title="t('nova.dashboard.title')">
    <template #subtitle>
      <template v-if="config.loading.value"><NvSkeleton width="240px" /></template>
      <template v-else-if="config.data.value">
        <NvStatusDot status="success" :label="t('nova.dashboard.host_online', { host: hostName })" />
        <span class="nv-muted" aria-hidden="true">·</span>
        <span class="nv-mono nv-dash__version">{{ t('nova.dashboard.version', { version }) }}</span>
      </template>
    </template>
    <template #actions>
      <NvButton variant="secondary" @click="restartOpen = true">{{ t('nova.dashboard.restart') }}</NvButton>
      <NvButton variant="primary" to="/pair">{{ t('nova.dashboard.pair') }}</NvButton>
    </template>

    <NvAlert v-if="config.error.value" variant="danger" :title="t('nova.common.load_failed')" live>
      <template #actions><NvButton size="sm" variant="secondary" @click="config.reload()">{{ t('nova.common.retry') }}</NvButton></template>
    </NvAlert>

    <div class="nv-dash">
      <NvCard :title="t('nova.dashboard.now_streaming')" :span="2">
        <!-- TODO(phase 2): replace with live session stats once the host exposes a sessions API
             (resolution, fps, codec, bitrate, per-stage latency, stop stream). -->
        <NvEmptyState compact :title="t('nova.dashboard.nobody_streaming')" :description="t('nova.dashboard.nobody_streaming_desc')">
          <template #icon><RadioTower :size="28" /></template>
        </NvEmptyState>
      </NvCard>

      <NvCard :title="t('nova.dashboard.host')">
        <div v-if="config.loading.value || logs.loading.value" aria-busy="true"><NvSkeleton :lines="6" /></div>
        <NvDataList v-else :items="hostItems" term-width="152px" />
        <template #footer><RouterLink to="/settings#encoder">{{ t('nova.dashboard.encoder_settings') }}</RouterLink></template>
      </NvCard>

      <NvCard :title="t('nova.dashboard.health')">
        <div v-if="logs.loading.value" aria-busy="true"><NvSkeleton :lines="4" /></div>
        <NvAlert v-else-if="logs.error.value" variant="danger" :title="t('nova.common.load_failed')">
          <template #actions><NvButton size="sm" variant="secondary" @click="logs.reload()">{{ t('nova.common.retry') }}</NvButton></template>
        </NvAlert>
        <ul v-else class="nv-health">
          <li v-for="item in health" :key="item.id" :class="['nv-health__item', `nv-health__item--${item.status}`]">
            <component :is="healthIcons[item.status]" :size="20" class="nv-health__icon" aria-hidden="true" />
            <div class="nv-health__text">
              <span class="nv-health__title">{{ item.title }}</span>
              <span v-if="item.desc" class="nv-health__desc">{{ item.desc }}</span>
              <RouterLink v-if="item.to" :to="item.to" class="nv-health__link">{{ item.linkLabel }}</RouterLink>
              <a v-else-if="item.href" :href="item.href" target="_blank" rel="noopener" class="nv-health__link">{{ t('nova.dashboard.release_notes') }}</a>
            </div>
          </li>
          <li v-if="health.length === 0" class="nv-health__none">{{ t('nova.dashboard.health_none') }}</li>
        </ul>
      </NvCard>

      <NvCard :title="t('nova.dashboard.devices')">
        <template #actions>
          <span v-if="clients.data.value?.length" class="nv-muted nv-dash__count">{{ t('nova.dashboard.devices_count', { count: clients.data.value.length }) }}</span>
        </template>
        <div v-if="clients.loading.value" aria-busy="true"><NvSkeleton :lines="3" height="18px" /></div>
        <NvAlert v-else-if="clients.error.value" variant="danger" :title="t('nova.common.load_failed')">
          <template #actions><NvButton size="sm" variant="secondary" @click="clients.reload()">{{ t('nova.common.retry') }}</NvButton></template>
        </NvAlert>
        <NvEmptyState v-else-if="!clients.data.value?.length" compact :title="t('nova.dashboard.no_devices')" :description="t('nova.dashboard.no_devices_desc')">
          <template #actions><NvButton size="sm" variant="primary" to="/pair">{{ t('nova.dashboard.pair') }}</NvButton></template>
        </NvEmptyState>
        <ul v-else class="nv-list">
          <li v-for="client in clients.data.value.slice(0, PREVIEW)" :key="client.uuid" class="nv-list__row">
            <span class="nv-list__name">{{ client.name || client.uuid }}</span>
            <NvBadge :variant="client.enabled === false ? 'neutral' : 'accent'">
              {{ client.enabled === false ? t('nova.devices.status_disabled') : t('nova.devices.status_enabled') }}
            </NvBadge>
          </li>
        </ul>
        <template #footer><RouterLink to="/devices">{{ t('nova.dashboard.manage_devices') }}</RouterLink></template>
      </NvCard>

      <NvCard :title="t('nova.dashboard.applications')">
        <template #actions><RouterLink to="/apps" class="nv-dash__count">{{ t('nova.dashboard.view_all') }}</RouterLink></template>
        <div v-if="apps.loading.value" aria-busy="true"><NvSkeleton :lines="3" height="18px" /></div>
        <NvAlert v-else-if="apps.error.value" variant="danger" :title="t('nova.common.load_failed')">
          <template #actions><NvButton size="sm" variant="secondary" @click="apps.reload()">{{ t('nova.common.retry') }}</NvButton></template>
        </NvAlert>
        <NvEmptyState v-else-if="!apps.data.value?.length" compact :title="t('nova.dashboard.no_apps')" :description="t('nova.dashboard.no_apps_desc')">
          <template #actions><NvButton size="sm" variant="secondary" to="/apps">{{ t('nova.dashboard.manage_apps') }}</NvButton></template>
        </NvEmptyState>
        <ul v-else class="nv-list">
          <li v-for="(app, i) in apps.data.value.slice(0, PREVIEW)" :key="`${i}-${app.name}`" class="nv-list__row">
            <span class="nv-list__name">{{ app.name }}</span>
          </li>
        </ul>
        <template #footer>
          <NvButton size="sm" variant="secondary" :loading="closing" @click="closeApp">{{ t('nova.dashboard.close_app') }}</NvButton>
        </template>
      </NvCard>
    </div>

    <NvDialog v-model:open="restartOpen" :title="t('nova.dashboard.restart_title')" :description="t('nova.dashboard.restart_desc')">
      <template #footer>
        <NvButton variant="secondary" @click="restartOpen = false">{{ t('nova.common.cancel') }}</NvButton>
        <NvButton variant="danger-solid" :loading="restarting" @click="restart">{{ t('nova.dashboard.restart_confirm') }}</NvButton>
      </template>
    </NvDialog>
  </NvPage>
</template>

<style>
@layer components {
  .nv-dash {
    display: grid;
    grid-template-columns: repeat(3, minmax(0, 1fr));
    gap: var(--nv-space-5);
    align-items: start;
  }

  .nv-dash__version {
    font-size: var(--nv-text-sm);
  }

  .nv-dash__count {
    font-size: var(--nv-text-sm);
  }

  .nv-health,
  .nv-list {
    display: flex;
    flex-direction: column;
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-health {
    gap: var(--nv-space-3);
  }

  .nv-health__item {
    display: flex;
    gap: var(--nv-space-3);
  }

  .nv-health__item--warning,
  .nv-health__item--danger {
    margin: 0 calc(-1 * var(--nv-space-3));
    padding: var(--nv-space-3);
    border-radius: var(--nv-radius-md);
    background: var(--nv-warning-tint);
  }

  .nv-health__item--danger {
    background: var(--nv-danger-tint);
  }

  .nv-health__icon {
    flex-shrink: 0;
    margin-top: 1px;
    color: var(--nv-success);
  }

  .nv-health__item--warning .nv-health__icon,
  .nv-health__item--warning .nv-health__title,
  .nv-health__item--warning .nv-health__link {
    color: var(--nv-warning);
  }

  .nv-health__item--danger .nv-health__icon,
  .nv-health__item--danger .nv-health__title,
  .nv-health__item--danger .nv-health__link {
    color: var(--nv-danger);
  }

  .nv-health__text {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
  }

  .nv-health__title {
    font-weight: 500;
  }

  .nv-health__desc {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-health__item--warning .nv-health__desc,
  .nv-health__item--danger .nv-health__desc {
    color: var(--nv-text);
  }

  .nv-health__link {
    font-size: var(--nv-text-sm);
    font-weight: 500;
  }

  .nv-health__none {
    color: var(--nv-text-secondary);
  }

  .nv-list__row {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 48px;
    border-bottom: 1px solid var(--nv-border);
  }

  .nv-list__row:last-child {
    border-bottom: 0;
  }

  .nv-list__name {
    flex-grow: 1;
    min-width: 0;
    font-weight: 500;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  @media (max-width: 1199px) {
    .nv-dash {
      grid-template-columns: repeat(2, minmax(0, 1fr));
    }
  }

  @media (max-width: 899px) {
    .nv-dash {
      grid-template-columns: minmax(0, 1fr);
      gap: var(--nv-space-4);
    }
  }
}
</style>

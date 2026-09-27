<script setup>
/**
 * Paired devices as a table (SPEC §5 Table row): device (icon, name, status line), access and a
 * ⋯ menu. Clicking a row or its name opens the details panel; the streaming device is marked in
 * success and lists its live numbers. A search box appears from 8 devices.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { ChevronRight, Smartphone } from '@lucide/vue'
import NvTable from '../components/NvTable.vue'
import NvTextField from '../components/NvTextField.vue'
import NvButton from '../components/NvButton.vue'
import NvEmptyState from '../components/NvEmptyState.vue'
import NvActionMenu from '../components/NvActionMenu.vue'
import { absoluteTime, filterDevices, permissionPreset, relativeTime, sessionFor } from './format'
import { sessionSummary } from '../live'

/** Show the search box from this many devices on. */
const SEARCH_FROM = 8

const props = defineProps({
  devices: { type: Array, required: true },
  sessions: { type: Array, default: () => [] },
  selected: { type: String, default: '' },
  busy: { type: Object, default: () => ({}) },
})
const query = defineModel('query', { type: String, default: '' })
const emit = defineEmits(['open', 'toggle', 'copy-id', 'disconnect', 'unpair'])

const { t, locale } = useI18n()

const showSearch = computed(() => props.devices.length >= SEARCH_FROM || query.value !== '')
const visible = computed(() => filterDevices(props.devices, query.value))
const resultText = computed(() => (query.value
  ? t('nova.devices.search_results', { n: visible.value.length, total: props.devices.length }, visible.value.length)
  : ''))
const columns = computed(() => [
  { key: 'device', label: t('nova.devices.col_device') },
  { key: 'access', label: t('nova.devices.col_access'), width: '200px' },
  { key: 'actions', label: t('nova.devices.col_actions'), srOnly: true, align: 'end', width: '64px' },
])

/**
 * Display data for one row.
 *
 * @param {object} device A device.
 * @returns {object} Name, status line, whether it is live, access label and menu items.
 */
function view(device) {
  const session = sessionFor(props.sessions, device.uuid)
  const live = !!(session || device.connected)
  const enabled = device.enabled !== false
  const name = device.name || t('nova.devices.unnamed')
  let status
  if (live) status = session?.app_name ? t('nova.devices.streaming_app', { app: session.app_name }) : t('nova.devices.streaming_now')
  else if (device.last_connected_at) status = t('nova.devices.last_connected', { when: relativeTime(device.last_connected_at, locale.value) })
  else status = t('nova.devices.never_connected')
  if (!enabled) status = `${t('nova.devices.status_disabled')} · ${status}`
  const preset = permissionPreset(device.permissions)
  const access = enabled ? t(`nova.devices.access_${preset}`) : t('nova.devices.access_none')
  const accessShort = enabled ? t(`nova.devices.access_short_${preset}`) : t('nova.devices.access_short_none')
  const items = [
    { id: 'open', label: t('nova.devices.menu_details'), onSelect: () => emit('open', device.uuid) },
    {
      id: 'toggle',
      label: enabled ? t('nova.devices.menu_block') : t('nova.devices.menu_allow'),
      disabled: !!props.busy[device.uuid],
      onSelect: () => emit('toggle', device.uuid, !enabled),
    },
    { id: 'copy', label: t('nova.devices.menu_copy_id'), onSelect: () => emit('copy-id', device.uuid) },
  ]
  if (live) items.push({ id: 'disconnect', label: t('nova.devices.disconnect'), onSelect: () => emit('disconnect', device.uuid) })
  items.push({ id: 'div', divider: true }, { id: 'unpair', label: t('nova.devices.unpair_ellipsis'), danger: true, onSelect: () => emit('unpair', device.uuid) })
  return {
    name,
    status,
    statusTitle: absoluteTime(device.last_connected_at, locale.value) || null,
    live,
    stats: live ? sessionSummary(session) : '',
    access,
    accessShort,
    items,
  }
}

const rows = computed(() => visible.value.map((d) => ({ ...d, view: view(d) })))
</script>

<template>
  <div class="nv-dtable">
    <div v-if="showSearch" class="nv-dtable__toolbar" role="search">
      <NvTextField v-model="query" type="search" :label="t('nova.devices.search_label')" hide-label
                   :placeholder="t('nova.devices.search_placeholder')" autocomplete="off" />
      <p class="nv-visually-hidden" role="status">{{ resultText }}</p>
    </div>

    <NvEmptyState v-if="!visible.length" compact :title="t('nova.devices.no_matches', { q: query })">
      <template #actions><NvButton size="sm" variant="secondary" @click="query = ''">{{ t('nova.devices.clear_search') }}</NvButton></template>
    </NvEmptyState>

    <div v-else class="nv-dtable__card">
      <NvTable :columns="columns" :rows="rows" row-key="uuid" :selected-key="selected || null"
               :caption="t('nova.devices.paired_title')" :row-height="60" @row-click="(row) => emit('open', row.uuid)">
        <template #cell-device="{ row }">
          <span class="nv-drow">
            <span :class="['nv-drow__icon', { 'nv-drow__icon--live': row.view.live }]" aria-hidden="true">
              <Smartphone :size="16" />
            </span>
            <span class="nv-drow__text">
              <button type="button" class="nv-drow__name" @click.stop="emit('open', row.uuid)">{{ row.view.name }}</button>
              <span :class="['nv-drow__status', { 'nv-drow__status--live': row.view.live }]" :title="row.view.statusTitle">
                {{ row.view.status }}<span v-if="row.view.stats" class="nv-drow__stats"> · <span class="nv-mono">{{ row.view.stats }}</span></span>
              </span>
            </span>
          </span>
        </template>
        <template #cell-access="{ row }">
          <span :class="['nv-drow__access', { 'nv-drow__access--none': row.enabled === false }]">
            <span class="nv-drow__access-long">{{ row.view.access }}</span>
            <span class="nv-drow__access-short" aria-hidden="true">{{ row.view.accessShort }}</span>
            <ChevronRight :size="16" class="nv-drow__chevron" aria-hidden="true" />
          </span>
        </template>
        <template #cell-actions="{ row }">
          <span class="nv-drow__menu" @click.stop>
            <NvActionMenu :label="t('nova.devices.actions_for', { name: row.view.name })" :items="row.view.items" />
          </span>
        </template>
      </NvTable>
    </div>
  </div>
</template>

<style scoped>
@layer components {
  .nv-dtable {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
  }

  .nv-dtable__toolbar {
    max-width: 360px;
  }

  .nv-dtable__card {
    overflow: hidden;
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-xl);
    background: var(--nv-surface);
  }

  .nv-dtable__card :deep(.nv-table__row) {
    cursor: pointer;
  }

  .nv-drow {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-width: 0;
  }

  .nv-drow__icon {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
    width: 36px;
    height: 36px;
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
    color: var(--nv-text-secondary);
  }

  .nv-drow__icon--live {
    color: var(--nv-success);
  }

  .nv-drow__text {
    display: flex;
    flex-direction: column;
    min-width: 0;
  }

  .nv-drow__name {
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

  .nv-drow__name:hover {
    text-decoration: underline;
  }

  .nv-drow__status {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
    font-variant-numeric: tabular-nums;
  }

  .nv-drow__status--live {
    color: var(--nv-success);
  }

  .nv-drow__access {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-drow__access--none {
    color: var(--nv-text-muted);
  }

  .nv-drow__menu {
    display: inline-flex;
  }

  .nv-drow__access {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-2);
  }

  .nv-drow__access-short,
  .nv-drow__chevron {
    display: none;
  }

  @media (max-width: 767px) {
    .nv-dtable__card :deep(.nv-table__row) {
      display: flex;
      align-items: center;
      gap: var(--nv-space-3);
      min-height: 64px;
    }

    .nv-dtable__card :deep(.nv-table__row > td) {
      width: auto;
    }

    .nv-dtable__card :deep(.nv-table__row > td:first-child) {
      flex-grow: 1;
      min-width: 0;
    }

    .nv-dtable__card :deep(.nv-table__row > td::before) {
      content: none;
    }

    .nv-dtable__card :deep(.nv-table__row > td:last-child),
    .nv-drow__icon,
    .nv-drow__stats,
    .nv-drow__access-long {
      display: none;
    }

    .nv-drow__access-short,
    .nv-drow__chevron {
      display: inline;
    }
  }
}
</style>

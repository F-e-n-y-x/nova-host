<script setup>
/**
 * Developer-only component gallery (/__components). Only routed when the build sets
 * VITE_NOVA_GALLERY=1 or in dev mode; production builds don't include it.
 */
import { computed, ref } from 'vue'
import { Cpu, Gauge, MonitorPlay, Radio, ShieldCheck, Smartphone, Trash2, Pencil, Plus } from '@lucide/vue'
import NvPage from '../../components/NvPage.vue'
import NvCard from '../../components/NvCard.vue'
import NvButton from '../../components/NvButton.vue'
import NvIconButton from '../../components/NvIconButton.vue'
import NvBadge from '../../components/NvBadge.vue'
import NvStatusDot from '../../components/NvStatusDot.vue'
import NvTextField from '../../components/NvTextField.vue'
import NvSelect from '../../components/NvSelect.vue'
import NvNumberField from '../../components/NvNumberField.vue'
import NvSwitch from '../../components/NvSwitch.vue'
import NvSegmentedControl from '../../components/NvSegmentedControl.vue'
import NvCheckbox from '../../components/NvCheckbox.vue'
import NvFilterChips from '../../components/NvFilterChips.vue'
import NvSettingRow from '../../components/NvSettingRow.vue'
import NvPathField from '../../components/NvPathField.vue'
import NvAlert from '../../components/NvAlert.vue'
import NvEmptyState from '../../components/NvEmptyState.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import NvDataList from '../../components/NvDataList.vue'
import NvTable from '../../components/NvTable.vue'
import NvActionMenu from '../../components/NvActionMenu.vue'
import NvStatBand from '../../components/NvStatBand.vue'
import NvAttention from '../../components/NvAttention.vue'
import NvSparkline from '../../components/NvSparkline.vue'
import NvArt from '../../components/NvArt.vue'
import NvSheet from '../../components/NvSheet.vue'
import NvConfirmDialog from '../../components/NvConfirmDialog.vue'
import NvLiveStrip from '../../components/NvLiveStrip.vue'
import { toast } from '../../toast'

const text = ref('atom')
const password = ref('hunter2hunter2')
const select = ref('nvenc')
const fps = ref(0)
const on = ref(true)
const off = ref(false)
const seg = ref('auto')
const check = ref(true)
const chips = ref('all')
const path = ref('/usr/bin/steam')
const sort = ref({ key: 'name', dir: 'asc' })
const sheetOpen = ref(false)
const confirmOpen = ref(false)
const selected = ref('gta')

const cells = [
  { key: 'stream', label: 'Stream', icon: Radio, value: 'Ready', status: 'success', sub: 'Waiting for a device' },
  { key: 'enc', label: 'Encoder', icon: Cpu, value: 'NVENC', sub: 'H.264 · HEVC' },
  { key: 'cap', label: 'Capture', icon: MonitorPlay, value: 'NvFBC', sub: 'Zero-copy · 60 Hz' },
  { key: 'dev', label: 'Devices', icon: Smartphone, value: '3 paired', sub: 'All allowed' },
  { key: 'ver', label: 'Nova', icon: ShieldCheck, value: '0.1.0', sub: 'Up to date' },
]
const issues = [
  { id: 'clip', title: 'Clipboard sync can’t run', detail: 'xclip isn’t installed on this X11 desktop.', command: 'sudo apt install xclip', action: { label: 'Clipboard settings', to: '/settings#clipboard_sync' } },
  { id: 'wan', title: 'Web UI is reachable from the internet', detail: 'Anyone who can reach this port sees the sign-in page.', action: { label: 'Network settings', to: '/settings#origin_web_ui_allowed' } },
]
const rows = [
  { id: 'gta', name: 'Grand Theft Auto V', type: 'Game', display: 'Matches the client', status: 'Running' },
  { id: 'wukong', name: 'Black Myth: Wukong', type: 'Game', display: 'Matches the client' },
  { id: 'desk', name: 'Desktop (Extend)', type: 'Desktop', display: 'Extend' },
]
const sortedRows = computed(() => [...rows].sort((a, b) => (sort.value.dir === 'asc' ? 1 : -1) * a[sort.value.key].localeCompare(b[sort.value.key])))
const rowMenu = [{ id: 'edit', label: 'Edit', icon: Pencil }, { divider: true, id: 's' }, { id: 'del', label: 'Delete…', icon: Trash2, danger: true }]
const session = {
  id: 's1', client_name: '[Paired device]', app_name: 'Grand Theft Auto V', fps_actual: 60, bitrate_kbps: 30000,
  latency_ms: { capture: 1.2, encode: 3.8, send: 1, total: 6 }, loss_pct: 0.1,
  samples: [28, 30, 27, 31, 29, 32, 30, 31, 29, 33, 31, 30].map((m) => ({ bitrate_kbps: m * 1000 })),
}
const series = session.samples.map((s) => s.bitrate_kbps / 1000)
const games = ['Grand Theft Auto V', 'Black Myth: Wukong', 'Marvel’s Spider-Man 2', 'Far Cry 5', 'Immortals Fenyx Rising', 'Nuvio']
</script>

<template>
  <NvPage title="Components" grid>
    <template #subtitle>Nova design system · dev only</template>
    <template #actions>
      <NvActionMenu label="More actions" :items="[{ id: 'a', label: 'Export' }, { id: 'b', label: 'Reset…', danger: true }]" />
      <NvButton variant="primary" :icon="Plus">Pair device</NvButton>
    </template>

    <NvStatBand :cells="cells" label="Status" />
    <NvAttention :issues="issues" />
    <NvLiveStrip :session="session" />

    <NvCard title="Library" :cols="8" flush>
      <template #actions><span class="nv-muted">10 applications</span><a href="#">View all</a></template>
      <NvTable v-model:sort="sort" :columns="[{ key: 'name', label: 'Name', sortable: true }, { key: 'type', label: 'Type', sortable: true, hideBelow: 1024 }, { key: 'display', label: 'Display' }, { key: 'actions', label: 'Actions', srOnly: true, align: 'end', width: '56px' }]"
               :rows="sortedRows" :selected-key="selected" caption="Library" @row-click="(r) => (selected = r.id)">
        <template #cell-name="{ row }">
          <span class="gal-name"><NvArt :title="row.name" kind="icon" decorative class="gal-icon" />
            <span><span class="gal-title">{{ row.name }}</span><span v-if="row.status" class="gal-running">{{ row.status }}</span></span></span>
        </template>
        <template #cell-actions="{ row }"><NvActionMenu :label="`Actions for ${row.name}`" :items="rowMenu" /></template>
      </NvTable>
    </NvCard>

    <NvCard title="Hardware" :cols="4">
      <template #actions><a href="#">Encoder settings</a></template>
      <NvDataList :items="[{ term: 'GPU', value: 'GeForce GTX 1080 Ti' }, { term: 'Driver', value: '580.178.04', mono: true }, { term: 'AV1', value: 'Needs an RTX 40-series GPU', muted: true }, { term: 'Display', value: 'HDMI-0 · 1920×1080 · 60 Hz' }]" />
      <div class="gal-row"><NvSparkline :values="series" label="Network over the last minute, now 30 Mbps" fill /><span class="nv-mono nv-secondary">31 Mbps</span></div>
    </NvCard>

    <NvCard title="Buttons" :cols="6">
      <div class="gal-row">
        <NvButton variant="primary">Save</NvButton>
        <NvButton>Cancel</NvButton>
        <NvButton variant="ghost">Session details</NvButton>
        <NvButton variant="danger">End stream…</NvButton>
        <NvButton variant="danger-solid">Unpair</NvButton>
      </div>
      <div class="gal-row">
        <NvButton size="sm">Small</NvButton>
        <NvButton size="lg" variant="primary">Large 44</NvButton>
        <NvButton loading>Saving</NvButton>
        <NvButton disabled>Disabled</NvButton>
        <NvIconButton label="Refresh preview"><Gauge :size="18" aria-hidden="true" /></NvIconButton>
      </div>
      <div class="gal-row">
        <NvBadge>Disabled</NvBadge><NvBadge variant="success">Streaming now</NvBadge><NvBadge variant="warning">Needs attention</NvBadge>
        <NvBadge variant="danger">Live</NvBadge><NvBadge variant="accent">Selected</NvBadge><NvBadge variant="info" class="nv-badge--outline">Steam</NvBadge>
        <NvStatusDot status="success" label="Ready to stream" />
      </div>
    </NvCard>

    <NvCard title="Inputs" :cols="6">
      <div class="gal-grid2">
        <NvTextField v-model="text" label="Host name" hint="Shown to devices on your network." />
        <NvTextField v-model="password" label="Password" type="password" autocomplete="new-password" />
        <NvSelect v-model="select" label="Encoder" :options="[{ value: 'nvenc', label: 'NVIDIA NVENC' }, { value: 'software', label: 'Software' }]" />
        <NvNumberField v-model="fps" label="Maximum frame rate" unit="fps" :min="0" :max="1000" />
        <NvTextField model-value="70000" label="Port" error="Use a port between 1024 and 65535." />
        <NvPathField v-model="path" label="Command" type="executable" />
      </div>
      <div class="gal-row">
        <NvSwitch v-model="on" label="Remote microphone" show-state />
        <NvSwitch v-model="off" label="Virtual display" show-state />
        <NvSegmentedControl v-model="seg" label="Capture pacing" :options="[{ value: 'auto', label: 'Auto' }, { value: 'vblank', label: 'Vblank' }, { value: 'timer', label: 'Timer' }]" />
        <NvCheckbox v-model="check" label="Select all" />
      </div>
      <NvFilterChips v-model="chips" label="Filter" :options="[{ value: 'all', label: 'All', count: 10 }, { value: 'games', label: 'Games', count: 7 }, { value: 'apps', label: 'Apps', count: 3 }]" />
    </NvCard>

    <NvCard title="Settings rows" :cols="8" flush>
      <NvSettingRow label="Capture pacing" description="Auto waits for the display’s refresh when the client’s frame rate fits it, and uses a timer otherwise." modified more="Vblank pacing is not available on NVIDIA X11; Nova falls back to the timer." @reset="seg = 'auto'">
        <template #default="{ labelId, descriptionId }">
          <NvSegmentedControl v-model="seg" :labelledby="labelId" :options="[{ value: 'auto', label: 'Auto' }, { value: 'vblank', label: 'Vblank' }, { value: 'timer', label: 'Timer' }]" :aria-describedby="descriptionId" />
        </template>
      </NvSettingRow>
      <NvSettingRow label="Remote microphone" description="Clients can send their microphone. Apps on this PC see it as “Nova Mic”.">
        <template #default="{ labelId }"><NvSwitch v-model="on" :labelledby="labelId" show-state /></template>
      </NvSettingRow>
      <NvSettingRow label="Clipboard sync" description="Copy on one device and paste on the other." warning="xclip isn’t installed, so sync can’t run on this X11 desktop.">
        <template #default="{ labelId }"><NvSwitch v-model="on" :labelledby="labelId" /></template>
      </NvSettingRow>
    </NvCard>

    <NvCard title="States" :cols="4">
      <NvAlert variant="danger" title="Can’t reach the Nova host">Check that it’s running, then try again.
        <template #actions><NvButton size="sm">Try again</NvButton></template></NvAlert>
      <NvAlert variant="success">Saved. Changes apply after Nova restarts.</NvAlert>
      <NvSkeleton :lines="3" />
      <NvEmptyState title="No games yet" description="Scan a folder or import your Lutris and Steam libraries." compact>
        <template #actions><NvButton variant="primary">Add games</NvButton></template>
      </NvEmptyState>
    </NvCard>

    <NvCard title="Posters" :cols="12">
      <div class="gal-posters"><NvArt v-for="g in games" :key="g" :title="g" /></div>
    </NvCard>

    <NvCard title="Overlays" :cols="12">
      <div class="gal-row">
        <NvButton @click="sheetOpen = true">Open side panel</NvButton>
        <NvButton variant="danger" @click="confirmOpen = true">Unpair all…</NvButton>
        <NvButton @click="toast.success('Saved Grand Theft Auto V.')">Success toast</NvButton>
        <NvButton @click="toast.info('Deleted Portal.', { action: { label: 'Undo', onClick: () => toast.success('Restored Portal.') } })">Undo toast</NvButton>
        <NvButton @click="toast.danger('Couldn’t save. The host didn’t answer.')">Error toast</NvButton>
      </div>
    </NvCard>

    <NvSheet v-model:open="sheetOpen" title="Grand Theft Auto V" subtitle="Folder · /DATA/Games/GTA V">
      <NvArt title="Grand Theft Auto V" kind="hero" />
      <NvTextField v-model="text" label="Name" />
      <NvPathField v-model="path" label="Command" type="executable" />
      <template #footer-start><NvActionMenu label="More" :items="rowMenu" /></template>
      <template #footer><NvButton @click="sheetOpen = false">Cancel</NvButton><NvButton variant="primary">Save</NvButton></template>
    </NvSheet>
    <NvConfirmDialog v-model:open="confirmOpen" title="Unpair all devices?" description="Every device will have to pair again before it can stream."
                     confirm-label="Unpair all" require-check="I understand every device will be unpaired" @confirm="confirmOpen = false" />
  </NvPage>
</template>

<style>
@layer components {
  .gal-row {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: var(--nv-space-3);
  }

  .gal-grid2 {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: var(--nv-space-4);
  }

  .gal-name {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
  }

  .gal-icon {
    width: 32px;
  }

  .gal-title {
    display: block;
    font-weight: 500;
    color: var(--nv-text);
  }

  .gal-running {
    display: block;
    font-size: var(--nv-text-xs);
    color: var(--nv-success);
  }

  .gal-posters {
    display: grid;
    grid-template-columns: repeat(6, minmax(0, 1fr));
    gap: var(--nv-space-4);
  }

  @media (max-width: 767px) {
    .gal-grid2 {
      grid-template-columns: minmax(0, 1fr);
    }

    .gal-posters {
      grid-template-columns: repeat(3, minmax(0, 1fr));
    }
  }
}
</style>

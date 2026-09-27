<script setup>
/**
 * Extra help shown under a setting's "More": commands to find device names, what the
 * host logs about displays, and the ports derived from the base port.
 *
 * Props: topic ('adapter_name' | 'output_name' | 'audio_sink' | 'steamgriddb' | 'igdb' | 'rawg' | 'ports'), platform, config.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'

const props = defineProps({
  topic: { type: String, required: true },
  platform: { type: String, required: true },
  config: { type: Object, required: true },
})
const { t } = useI18n()

const DEFAULT_PORT = 47989
const unix = computed(() => props.platform === 'linux' || props.platform === 'freebsd')
const port = computed(() => {
  const n = Number(props.config.port)
  return Number.isFinite(n) && n > 0 ? n : DEFAULT_PORT
})
const ports = computed(() => [
  { protocol: t('config.port_tcp'), port: String(port.value - 5), note: '' },
  { protocol: t('config.port_tcp'), port: String(port.value), note: t('config.port_http_port_note'), strong: true },
  { protocol: t('config.port_tcp'), port: String(port.value + 1), note: t('config.port_web_ui') },
  { protocol: t('config.port_tcp'), port: String(port.value + 21), note: '' },
  { protocol: t('config.port_udp'), port: `${port.value + 9}–${port.value + 11}`, note: '' },
])

const VAINFO = 'vainfo --display drm --device /dev/dri/renderD129 | \\\n  grep -E "((VAProfileH264High|VAProfileHEVCMain|VAProfileHEVCMain10).*VAEntrypointEncSlice)|Driver version"'
const DISPLAY_LOG = {
  unix: 'Info: Detecting displays\nInfo: Detected display: HDMI-A-1 connected: true\nInfo: Detected display: DP-1 connected: true\nInfo: Detected display: DP-2 connected: false',
  macos: 'Info: Detecting displays\nInfo: Detected display: Monitor-0 (id: 3) connected: true\nInfo: Detected display: Monitor-1 (id: 2) connected: true',
  windows: '{\n  "device_id": "{de9bb7e2-186e-505b-9e93-f48793333810}",\n  "display_name": "\\\\.\\DISPLAY1",\n  "friendly_name": "ROG PG279Q"\n}',
}
</script>

<template>
  <div class="nv-cfg-help">
    <template v-if="topic === 'adapter_name'">
      <template v-if="platform === 'windows'">
        <pre class="nv-cfg-help__code">tools\dxgi-info.exe</pre>
      </template>
      <template v-else-if="unix">
        <pre class="nv-cfg-help__code">ls /dev/dri/renderD*  # {{ t('config.adapter_name_desc_linux_2') }}</pre>
        <pre class="nv-cfg-help__code">{{ VAINFO }}</pre>
        <p>{{ t('config.adapter_name_desc_linux_3') }}</p>
        <pre class="nv-cfg-help__code">VAProfileH264High   : VAEntrypointEncSlice</pre>
      </template>
    </template>

    <template v-else-if="topic === 'output_name'">
      <pre class="nv-cfg-help__code">{{ platform === 'windows' ? DISPLAY_LOG.windows : platform === 'macos' ? DISPLAY_LOG.macos : DISPLAY_LOG.unix }}</pre>
      <RouterLink class="nv-cfg-help__link" to="/logs">{{ t('nova.settings.help_open_logs') }}</RouterLink>
    </template>

    <template v-else-if="topic === 'audio_sink'">
      <pre v-if="platform === 'windows'" class="nv-cfg-help__code">tools\audio-info.exe</pre>
      <template v-else-if="platform === 'macos'">
        <p>
          <a href="https://github.com/ExistentialAudio/BlackHole" target="_blank" rel="noopener noreferrer">BlackHole</a>
          ·
          <a href="https://github.com/mattingalls/Soundflower" target="_blank" rel="noopener noreferrer">Soundflower</a>
        </p>
      </template>
      <template v-else>
        <pre class="nv-cfg-help__code">pactl list short sinks</pre>
        <pre class="nv-cfg-help__code">pactl info | grep Source</pre>
      </template>
    </template>

    <template v-else-if="topic === 'steamgriddb'">
      <p>{{ t('nova.settings.steamgriddb_help') }}</p>
      <a class="nv-cfg-help__link" href="https://www.steamgriddb.com/profile/preferences/api" target="_blank"
         rel="noopener noreferrer">{{ t('nova.settings.steamgriddb_get_key') }}</a>
    </template>

    <template v-else-if="topic === 'igdb'">
      <p>{{ t('nova.settings.igdb_help') }}</p>
      <a class="nv-cfg-help__link" href="https://dev.twitch.tv/console/apps/create" target="_blank"
         rel="noopener noreferrer">{{ t('nova.settings.igdb_get_key') }}</a>
    </template>

    <template v-else-if="topic === 'steam_web'">
      <p>{{ t('nova.settings.steam_web_help') }}</p>
      <a class="nv-cfg-help__link" href="https://steamcommunity.com/dev/apikey" target="_blank"
         rel="noopener noreferrer">{{ t('nova.settings.steam_web_get_key') }}</a>
    </template>

    <template v-else-if="topic === 'rawg'">
      <p>{{ t('nova.settings.rawg_help') }}</p>
      <a class="nv-cfg-help__link" href="https://rawg.io/apidocs" target="_blank"
         rel="noopener noreferrer">{{ t('nova.settings.rawg_get_key') }}</a>
    </template>

    <template v-else-if="topic === 'ports'">
      <table class="nv-cfg-help__table">
        <caption class="nv-visually-hidden">{{ t('nova.settings.ports_caption') }}</caption>
        <thead>
          <tr>
            <th scope="col">{{ t('config.port_protocol') }}</th>
            <th scope="col">{{ t('config.port_port') }}</th>
            <th scope="col">{{ t('config.port_note') }}</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="row in ports" :key="row.port">
            <td>{{ row.protocol }}</td>
            <td class="nv-cfg-help__mono">{{ row.port }}</td>
            <td :class="{ 'nv-cfg-help__strong': row.strong }">{{ row.note }}</td>
          </tr>
        </tbody>
      </table>
    </template>
  </div>
</template>

<style>
@layer components {
  .nv-cfg-help {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-cfg-help p {
    margin: 0;
  }

  .nv-cfg-help__code {
    margin: 0;
    padding: var(--nv-space-2) var(--nv-space-3);
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
    color: var(--nv-text);
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    white-space: pre-wrap;
    overflow-wrap: anywhere;
  }

  .nv-cfg-help__link {
    color: var(--nv-accent-text);
    font-weight: 500;
  }

  .nv-cfg-help__table {
    width: 100%;
    max-width: 520px;
    border-collapse: collapse;
  }

  .nv-cfg-help__table th,
  .nv-cfg-help__table td {
    padding: 6px var(--nv-space-2);
    border-bottom: 1px solid var(--nv-border);
    text-align: left;
  }

  .nv-cfg-help__table th {
    font-weight: 500;
    color: var(--nv-text-secondary);
  }

  .nv-cfg-help__table td {
    color: var(--nv-text);
  }

  .nv-cfg-help__mono {
    font-family: var(--nv-font-mono);
    font-variant-numeric: tabular-nums;
  }

  .nv-cfg-help__strong {
    font-weight: 500;
  }
}
</style>

<script setup>
/**
 * Collapsible reference of the environment variables every app command receives,
 * with a platform-specific example of matching the client's resolution.
 *
 * Props: platform (host platform).
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { project } from '../../project'

const props = defineProps({
  platform: { type: String, default: '' },
})
const { t } = useI18n()

const ENV_VARS = [
  ['SUNSHINE_APP_ID', 'apps.env_app_id'],
  ['SUNSHINE_APP_NAME', 'apps.env_app_name'],
  ['SUNSHINE_CLIENT_NAME', 'apps.env_client_name'],
  ['SUNSHINE_CLIENT_WIDTH', 'apps.env_client_width'],
  ['SUNSHINE_CLIENT_HEIGHT', 'apps.env_client_height'],
  ['SUNSHINE_CLIENT_FPS', 'apps.env_client_fps'],
  ['SUNSHINE_CLIENT_HDR', 'apps.env_client_hdr'],
  ['SUNSHINE_CLIENT_GCMAP', 'apps.env_client_gcmap'],
  ['SUNSHINE_CLIENT_HOST_AUDIO', 'apps.env_client_host_audio'],
  ['SUNSHINE_CLIENT_ENABLE_SOPS', 'apps.env_client_enable_sops'],
  ['SUNSHINE_CLIENT_AUDIO_CONFIGURATION', 'apps.env_client_audio_config'],
]

const example = computed(() => {
  if (props.platform === 'windows') {
    return 'cmd /C <qres path>\\QRes.exe /X:%SUNSHINE_CLIENT_WIDTH% /Y:%SUNSHINE_CLIENT_HEIGHT% /R:%SUNSHINE_CLIENT_FPS%'
  }
  if (props.platform === 'macos') {
    return 'sh -c "displayplacer "id:<screenId> res:${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT} hz:${SUNSHINE_CLIENT_FPS} scaling:on origin:(0,0) degree:0""'
  }
  return 'sh -c "xrandr --output HDMI-1 --mode \\"${SUNSHINE_CLIENT_WIDTH}x${SUNSHINE_CLIENT_HEIGHT}\\" --rate ${SUNSHINE_CLIENT_FPS}"'
})
</script>

<template>
  <details class="nv-env">
    <summary class="nv-env__summary">{{ t('nova.apps.env_title') }}</summary>
    <p class="nv-env__desc">{{ t('nova.apps.env_desc') }}</p>
    <table class="nv-env__table">
      <thead><tr><th scope="col">{{ t('apps.env_var_name') }}</th><th scope="col">{{ t('apps.env_var_description') }}</th></tr></thead>
      <tbody>
        <tr v-for="[name, key] in ENV_VARS" :key="name"><td><code class="nv-env__code">{{ name }}</code></td><td>{{ t(key) }}</td></tr>
      </tbody>
    </table>
    <p class="nv-env__desc">{{ t('nova.apps.env_example') }}</p>
    <pre class="nv-env__pre">{{ example }}</pre>
    <a :href="project.docsUrl" target="_blank" rel="noopener noreferrer" class="nv-env__link">{{ t('nova.apps.env_docs') }}</a>
  </details>
</template>

<style>
@layer components {
  .nv-env {
    border-top: 1px solid var(--nv-border);
    padding-top: var(--nv-space-4);
  }

  .nv-env__summary {
    cursor: pointer;
    font-weight: 500;
    min-height: 36px;
    display: flex;
    align-items: center;
  }

  .nv-env[open] > * + * {
    margin-top: var(--nv-space-3);
  }

  .nv-env__desc {
    margin-bottom: 0;
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  .nv-env__table {
    width: 100%;
    border-collapse: collapse;
    font-size: var(--nv-text-sm);
  }

  .nv-env__table th,
  .nv-env__table td {
    text-align: left;
    padding: var(--nv-space-2);
    border-bottom: 1px solid var(--nv-border);
    vertical-align: top;
  }

  .nv-env__code,
  .nv-env__pre {
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    word-break: break-all;
  }

  .nv-env__pre {
    margin-bottom: 0;
    padding: var(--nv-space-3);
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
    white-space: pre-wrap;
  }

  .nv-env__link {
    display: inline-block;
    color: var(--nv-accent-text);
    font-weight: 500;
  }
}
</style>

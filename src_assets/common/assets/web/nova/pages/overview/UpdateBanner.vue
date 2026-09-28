<script setup>
/**
 * Overview update banner. Shown only for a real update the host found (strictly newer nova-vX.Y.Z)
 * or while an install runs. "Install update" asks for confirmation; the host refuses while a device
 * streams, downloads the .deb, checks its SHA-256 and restarts Nova once nothing is streaming.
 */
import { computed, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { Copy, Download, ExternalLink } from '@lucide/vue'
import NvAlert from '../../components/NvAlert.vue'
import NvButton from '../../components/NvButton.vue'
import NvConfirmDialog from '../../components/NvConfirmDialog.vue'
import { toast } from '../../toast'

const props = defineProps({
  /** Status from GET /api/update/status. */
  status: { type: Object, default: null },
  /** Whether a device is streaming right now (from the live session feed). */
  streaming: { type: Boolean, default: false },
  /** A POST is in flight. */
  starting: { type: Boolean, default: false },
  /** 'conflict' or 'failed' after a refused install. */
  installError: { type: String, default: '' },
  /** Starts the install: (tag) => Promise<boolean>. */
  install: { type: Function, required: true },
})
const { t } = useI18n()

const latest = computed(() => props.status?.latest || null)
const state = computed(() => props.status?.install?.state || 'idle')
const available = computed(() => Boolean(props.status?.enabled && props.status?.update_available && latest.value?.tag))
const running = computed(() => ['downloading', 'verifying', 'installing', 'restarting'].includes(state.value))
const blocked = computed(() => props.streaming || Boolean(props.status?.streaming))
const visible = computed(() => available.value || running.value || state.value === 'manual' || state.value === 'failed')

const variant = computed(() => ({ failed: 'danger', manual: 'warning', restarting: 'success' }[state.value] || 'info'))
const title = computed(() => {
  const version = latest.value?.version || props.status?.install?.tag || ''
  if (state.value === 'downloading') {
    const pct = Math.round((props.status?.install?.progress || 0) * 100)
    return t('nova.update.downloading', { version, pct })
  }
  if (state.value === 'verifying') return t('nova.update.verifying', { version })
  if (state.value === 'installing') return t('nova.update.installing', { version })
  if (state.value === 'restarting') return t('nova.update.restarting', { version })
  if (state.value === 'manual') return t('nova.update.manual_title', { version })
  if (state.value === 'failed') return t('nova.update.failed_title', { version })
  return latest.value?.prerelease ? t('nova.dashboard.prerelease_available', { version }) : t('nova.dashboard.update_available', { version })
})

const confirmOpen = ref(false)
const dialogError = computed(() => {
  if (props.installError === 'conflict') return blocked.value ? t('nova.update.refused_streaming') : t('nova.update.refused')
  if (props.installError === 'failed') return t('nova.update.start_failed')
  return ''
})

async function confirm() {
  const ok = await props.install(latest.value.tag)
  if (ok) confirmOpen.value = false
}

async function copyCommand() {
  try {
    await navigator.clipboard.writeText(props.status?.install?.command || '')
    toast.success(t('nova.update.copied'))
  } catch {
    toast.danger(t('nova.update.copy_failed'))
  }
}
</script>

<template>
  <NvAlert v-if="visible" :variant="variant" :title="title" live class="nv-update">
    <template v-if="state === 'manual' || state === 'failed'">
      <p>{{ status.install.message }}</p>
      <code v-if="status.install.command" class="nv-update__cmd">{{ status.install.command }}</code>
    </template>
    <template v-else-if="state === 'restarting'">{{ status.install.message }}</template>
    <template v-else-if="!running">
      {{ t('nova.dashboard.update_available_desc', { current: status.current }) }}
      <template v-if="blocked"> {{ t('nova.update.wait_streaming') }}</template>
    </template>
    <template #actions>
      <NvButton v-if="latest?.html_url" size="sm" variant="ghost" :icon="ExternalLink" :href="latest.html_url">
        {{ t('nova.dashboard.release_notes') }}
      </NvButton>
      <NvButton v-if="state === 'manual' && status.install.command" size="sm" :icon="Copy" @click="copyCommand">
        {{ t('nova.update.copy_command') }}
      </NvButton>
      <NvButton v-if="available && !running" size="sm" variant="primary" :icon="Download" :disabled="blocked"
                @click="confirmOpen = true">
        {{ state === 'failed' ? t('nova.common.retry') : t('nova.update.install') }}
      </NvButton>
    </template>
  </NvAlert>
  <NvConfirmDialog v-model:open="confirmOpen" variant="primary"
                   :title="t('nova.update.confirm_title', { version: latest?.version || '' })"
                   :description="t('nova.update.confirm_desc', { current: status?.current || '' })"
                   :confirm-label="t('nova.update.install')" :loading="starting" :error="dialogError"
                   @confirm="confirm" />
</template>

<style>
@layer components {
  .nv-update__cmd {
    display: block;
    margin-top: var(--nv-space-2);
    padding: var(--nv-space-2) var(--nv-space-3);
    border-radius: var(--nv-radius-sm);
    background: var(--nv-bg);
    font-family: var(--nv-font-mono);
    font-size: 0.8125rem;
    overflow-wrap: anywhere;
  }
}
</style>

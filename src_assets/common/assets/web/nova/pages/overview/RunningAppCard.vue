<script setup>
/**
 * The app running on the host, streamed or not: "Running for 1 h 20 m", where it runs (Virtual
 * display or the desktop), who is connected, when an idle timeout will quit it, and Close.
 * A disconnect never ends the app, so this is how to see and end a game left running.
 *
 * Props: running (GET /api/apps/running body), icon (artwork URL or '').
 * Emits: close ({ name }).
 */
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import NvArt from '../../components/NvArt.vue'
import NvButton from '../../components/NvButton.vue'
import { formatRunningFor } from './format'

const props = defineProps({
  running: { type: Object, required: true },
  icon: { type: String, default: '' },
})
defineEmits(['close'])

const { t, locale } = useI18n()
const now = ref(Date.now())
let clock = null
onMounted(() => { clock = setInterval(() => { now.value = Date.now() }, 30000) })
onBeforeUnmount(() => clearInterval(clock))

const name = computed(() => props.running.app?.name || t('nova.live.unknown_app'))
const runningFor = computed(() => formatRunningFor(props.running.since, now.value))
const where = computed(() => (props.running.display === 'virtual' ? t('nova.overview.running_on_virtual') : t('nova.overview.running_on_desktop')))
const clients = computed(() => props.running.connected_clients || 0)
const who = computed(() => (clients.value > 0
  ? t('nova.overview.running_streaming', { n: clients.value }, clients.value)
  : t('nova.overview.running_nobody')))
const idleQuit = computed(() => {
  const at = props.running.idle_quit_at
  if (!Number.isFinite(at) || clients.value > 0) return ''
  const time = new Date(at * 1000).toLocaleString(locale.value, { weekday: 'short', hour: '2-digit', minute: '2-digit' })
  return t('nova.overview.running_idle_quit', { time, hours: props.running.idle_quit_hours })
})
</script>

<template>
  <section class="nv-run" aria-labelledby="nv-run-title">
    <NvArt :title="name" :src="icon" kind="icon" decorative class="nv-run__icon" />
    <div class="nv-run__who">
      <span class="nv-run__state"><span class="nv-run__dot" aria-hidden="true"></span>
        {{ runningFor ? t('nova.overview.running_for', { time: runningFor }) : t('nova.overview.running') }}</span>
      <h2 id="nv-run-title" class="nv-run__title">{{ name }}</h2>
      <span class="nv-run__meta">{{ where }} · {{ who }}</span>
      <span v-if="idleQuit" class="nv-run__meta">{{ idleQuit }}</span>
    </div>
    <div class="nv-run__actions">
      <NvButton variant="danger" @click="$emit('close', { name })">{{ t('nova.overview.close_app') }}</NvButton>
    </div>
  </section>
</template>

<style>
@layer components {
  .nv-run {
    display: flex;
    align-items: center;
    gap: var(--nv-space-4);
    min-height: 72px;
    padding: var(--nv-space-4) var(--nv-space-5);
    border-radius: var(--nv-radius-xl);
    border: 1px solid var(--nv-border);
    background: var(--nv-surface);
  }

  .nv-run__icon {
    flex-shrink: 0;
    width: 48px;
    height: 48px;
    border-radius: var(--nv-radius-md);
  }

  .nv-run__who {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
    flex: 1 1 auto;
  }

  .nv-run__state {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    font-size: var(--nv-text-xs);
    font-weight: 500;
    color: var(--nv-success);
    font-variant-numeric: tabular-nums;
  }

  .nv-run__dot {
    width: 6px;
    height: 6px;
    border-radius: 3px;
    background: var(--nv-success);
  }

  .nv-run__title {
    margin: 0;
    font-size: var(--nv-text-lg);
    font-weight: 600;
    line-height: 1.35;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-run__meta {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
  }

  .nv-run__actions {
    flex-shrink: 0;
  }

  @media (max-width: 767px) {
    .nv-run {
      flex-wrap: wrap;
      padding: var(--nv-space-4);
    }

    .nv-run__actions {
      flex-basis: 100%;
    }

    .nv-run__actions .nv-btn {
      width: 100%;
    }
  }
}
</style>

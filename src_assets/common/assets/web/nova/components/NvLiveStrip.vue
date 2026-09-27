<script setup>
/**
 * Compact live-stream strip (SPEC §4 "Live strip"): 48px card under the top bar on every page
 * except Overview while a stream runs — 40×28 thumbnail, "Live" in success, "<app> on <device>",
 * mono headline numbers, a 72×24 network sparkline and a "View stream" link to Overview.
 *
 * Props: session (required; from useLiveSession().current), thumbnail (optional image URL).
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import NvArt from './NvArt.vue'
import NvSparkline from './NvSparkline.vue'
import { bitrateSeries, sessionSummary } from '../live'

const props = defineProps({
  session: { type: Object, required: true },
  thumbnail: { type: String, default: '' },
})

const { t } = useI18n()
const title = computed(() => t('nova.live.title', {
  app: props.session.app_name || t('nova.live.unknown_app'),
  device: props.session.client_name || t('nova.live.unknown_device'),
}))
const series = computed(() => bitrateSeries(props.session))
const latestMbps = computed(() => {
  const vals = series.value.filter(Number.isFinite)
  return vals.length ? Math.round(vals[vals.length - 1]) : null
})
const sparkLabel = computed(() => (latestMbps.value === null
  ? t('nova.live.network_none')
  : t('nova.live.network_label', { mbps: latestMbps.value })))
</script>

<template>
  <section class="nv-live-strip" :aria-label="t('nova.live.region')">
    <NvArt :title="session.app_name || '?'" :src="thumbnail" kind="thumb" decorative class="nv-live-strip__thumb" />
    <span class="nv-live-strip__live"><span class="nv-live-strip__dot" aria-hidden="true"></span>{{ t('nova.live.live') }}</span>
    <span class="nv-live-strip__title">{{ title }}</span>
    <span class="nv-live-strip__stats nv-mono">{{ sessionSummary(session) }}</span>
    <NvSparkline :values="series" :label="sparkLabel" :width="72" :height="24" class="nv-live-strip__spark" />
    <RouterLink to="/" class="nv-live-strip__link">{{ t('nova.live.view_stream') }}</RouterLink>
  </section>
</template>

<style>
@layer components {
  .nv-live-strip {
    display: flex;
    align-items: center;
    gap: var(--nv-space-4);
    min-height: 48px;
    padding: 0 var(--nv-space-2) 0 var(--nv-space-3);
    border-radius: var(--nv-radius-xl);
    border: 1px solid var(--nv-border);
    background: var(--nv-surface);
    font-size: var(--nv-text-sm);
  }

  .nv-live-strip__thumb {
    width: 40px;
    height: 28px;
    border-radius: var(--nv-radius-sm);
  }

  .nv-live-strip__live {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-2);
    font-weight: 500;
    color: var(--nv-success);
  }

  .nv-live-strip__dot {
    width: 6px;
    height: 6px;
    border-radius: 3px;
    background: var(--nv-success);
  }

  .nv-live-strip__title {
    flex-grow: 1;
    min-width: 0;
    font-weight: 500;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-live-strip__stats {
    color: var(--nv-text-secondary);
    white-space: nowrap;
  }

  .nv-live-strip__link {
    display: inline-flex;
    align-items: center;
    min-height: 32px;
    padding: 0 10px;
    border-radius: var(--nv-radius-md);
    color: var(--nv-text-secondary);
    font-weight: 500;
    white-space: nowrap;
  }

  .nv-live-strip__link:hover {
    background: var(--nv-raised);
    color: var(--nv-text);
    text-decoration: none;
  }

  @media (max-width: 1023px) {
    .nv-live-strip__spark {
      display: none;
    }
  }

  @media (max-width: 767px) {
    .nv-live-strip {
      gap: var(--nv-space-3);
      border-radius: 0;
      border-width: 0 0 1px;
    }

    .nv-live-strip__thumb,
    .nv-live-strip__stats {
      display: none;
    }

    .nv-live-strip__link {
      min-height: 44px;
    }
  }
}
</style>

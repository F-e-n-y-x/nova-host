<script setup>
/**
 * Live stream bar (Final_Dashboard_Live): 86px row — thumbnail, "Live · 00:42:13", "<app> on <device>",
 * resolution / frame rate / codec / bitrate, network sparkline, End stream. Only rendered while a
 * stream runs. On phones it wraps into a compact card.
 *
 * Props: session (from /api/sessions), thumbnail (preview image URL), more (other live sessions).
 * Emits: end (parent confirms), details.
 */
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import NvArt from '../../components/NvArt.vue'
import NvButton from '../../components/NvButton.vue'
import NvSparkline from '../../components/NvSparkline.vue'
import { bitrateSeries } from '../../live'
import { formatClock, formatMbps } from './format'

const props = defineProps({
  session: { type: Object, required: true },
  thumbnail: { type: String, default: '' },
  more: { type: Number, default: 0 },
})
defineEmits(['end', 'details'])

const { t } = useI18n()
const now = ref(Date.now())
let clock = null
onMounted(() => { clock = setInterval(() => { now.value = Date.now() }, 1000) })
onBeforeUnmount(() => clearInterval(clock))

const app = computed(() => props.session.app_name || t('nova.live.unknown_app'))
const device = computed(() => props.session.client_name || t('nova.live.unknown_device'))
const elapsed = computed(() => formatClock(props.session.started_at, now.value))
const resolution = computed(() => {
  const r = props.session.resolution
  return r?.w && r?.h ? `${r.w}×${r.h}` : '—'
})
/** "Virtual display :20 2340×1080@120" when the stream captures a Nova virtual display. */
const virtualDisplay = computed(() => {
  const d = props.session.display
  if (d?.kind !== 'virtual' || !d.name) return ''
  const mode = d.w && d.h ? `${d.w}×${d.h}${d.fps ? `@${d.fps}` : ''}` : ''
  return t('nova.overview.virtual_display', { name: d.name, mode }).trim()
})
const fps = computed(() => (Number.isFinite(props.session.fps_actual) ? `${Math.round(props.session.fps_actual)} fps` : '—'))
const series = computed(() => bitrateSeries(props.session))
const latest = computed(() => {
  const vals = series.value.filter(Number.isFinite)
  return vals.length ? vals[vals.length - 1] : null
})
const sparkLabel = computed(() => (latest.value === null
  ? t('nova.live.network_none')
  : t('nova.live.network_label', { mbps: Math.round(latest.value) })))
</script>

<template>
  <section class="nv-sbar" aria-labelledby="nv-sbar-title">
    <div class="nv-sbar__thumb">
      <img v-if="thumbnail" :src="thumbnail" alt="" class="nv-sbar__img">
      <NvArt v-else :title="app" kind="fill" decorative :show-title="false" />
    </div>
    <div class="nv-sbar__who">
      <span class="nv-sbar__live"><span class="nv-sbar__dot" aria-hidden="true"></span>{{ t('nova.overview.live_for', { time: elapsed }) }}</span>
      <h2 id="nv-sbar-title" class="nv-sbar__title">{{ t('nova.live.title', { app, device }) }}</h2>
      <span v-if="virtualDisplay" class="nv-sbar__vd nv-mono" :title="t('nova.overview.virtual_display_hint')">{{ virtualDisplay }}</span>
      <span v-if="more > 0" class="nv-sbar__more">{{ t('nova.overview.more_streams', { n: more }, more) }}</span>
    </div>
    <dl class="nv-sbar__stats">
      <div><dt>{{ t('nova.overview.resolution') }}</dt><dd class="nv-mono">{{ resolution }}</dd></div>
      <div><dt>{{ t('nova.overview.frame_rate') }}</dt><dd class="nv-mono">{{ fps }}</dd></div>
      <div><dt>{{ t('nova.overview.codec') }}</dt><dd class="nv-mono">{{ session.codec || '—' }}</dd></div>
      <div><dt>{{ t('nova.overview.bitrate') }}</dt><dd class="nv-mono">{{ formatMbps(session.bitrate_kbps) }}</dd></div>
    </dl>
    <div class="nv-sbar__net">
      <span class="nv-sbar__netlabel"><span>{{ t('nova.overview.network') }}</span>
        <span class="nv-mono">{{ latest === null ? '—' : `${Math.round(latest)} Mbps` }}</span></span>
      <NvSparkline :values="series" :label="sparkLabel" :width="150" :height="30" />
    </div>
    <div class="nv-sbar__actions">
      <NvButton variant="danger" :disabled="!session.client_uuid" @click="$emit('end', session)">{{ t('nova.overview.end_stream') }}</NvButton>
    </div>
  </section>
</template>

<style>
@layer components {
  .nv-sbar {
    display: flex;
    align-items: center;
    gap: var(--nv-space-5);
    min-height: 86px;
    padding: var(--nv-space-4) var(--nv-space-5);
    border-radius: var(--nv-radius-xl);
    border: 1px solid var(--nv-border);
    background: var(--nv-surface);
  }

  .nv-sbar__thumb {
    position: relative;
    flex-shrink: 0;
    width: 96px;
    height: 54px;
    border-radius: var(--nv-radius-md);
    overflow: hidden;
    background: var(--nv-raised);
  }

  .nv-sbar__img {
    display: block;
    width: 100%;
    height: 100%;
    object-fit: cover;
  }

  .nv-sbar__who {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
    flex: 0 1 280px;
  }

  .nv-sbar__live {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    font-size: var(--nv-text-xs);
    font-weight: 500;
    color: var(--nv-success);
    font-variant-numeric: tabular-nums;
  }

  .nv-sbar__dot {
    width: 6px;
    height: 6px;
    border-radius: 3px;
    background: var(--nv-success);
  }

  .nv-sbar__title {
    margin: 0;
    font-size: var(--nv-text-lg);
    font-weight: 600;
    line-height: 1.35;
    display: -webkit-box;
    -webkit-line-clamp: 2;
    -webkit-box-orient: vertical;
    overflow: hidden;
    overflow-wrap: anywhere;
  }

  .nv-sbar__more {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
  }

  .nv-sbar__vd {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-sbar__stats {
    display: flex;
    gap: var(--nv-space-7);
    flex-grow: 1;
    margin: 0;
  }

  .nv-sbar__stats div {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .nv-sbar__stats dt {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
  }

  .nv-sbar__stats dd {
    margin: 0;
    font-size: var(--nv-text-md);
    white-space: nowrap;
  }

  .nv-sbar__net {
    display: flex;
    flex-direction: column;
    gap: 2px;
    width: 150px;
    flex-shrink: 0;
  }

  .nv-sbar__netlabel {
    display: flex;
    justify-content: space-between;
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
  }

  .nv-sbar__netlabel .nv-mono {
    color: var(--nv-text-secondary);
  }

  .nv-sbar__actions {
    flex-shrink: 0;
  }

  @media (max-width: 1279px) {
    .nv-sbar {
      flex-wrap: wrap;
      row-gap: var(--nv-space-4);
    }

    .nv-sbar__who {
      flex: 1 1 0;
    }

    .nv-sbar__stats {
      order: 5;
      flex-basis: 100%;
    }
  }

  @media (max-width: 767px) {
    .nv-sbar {
      gap: var(--nv-space-3);
      padding: var(--nv-space-4);
    }

    .nv-sbar__thumb {
      width: 72px;
      height: 40px;
    }

    .nv-sbar__stats {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: var(--nv-space-3);
    }

    .nv-sbar__net {
      order: 6;
      width: 100%;
    }

    .nv-sbar__net svg {
      width: 100%;
    }

    .nv-sbar__actions {
      order: 7;
      width: 100%;
    }

    .nv-sbar__actions .nv-btn {
      width: 100%;
    }
  }
}
</style>

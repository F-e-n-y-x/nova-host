<script setup>
/**
 * Stream health (Final_Dashboard, direction C): host latency split into capture / encode / send,
 * two metric cells and recent sessions. While streaming it shows the live session; idle, the
 * last finished session's averages (labelled as such — history has no per-stage split or loss).
 *
 * Props: session (live session or null), history (/api/sessions/history list, or null when the
 *        host has none), loading.
 * Emits: history (open the full history).
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import NvCard from '../../components/NvCard.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import { absoluteTime, relativeTime } from '../../devices/format'
import { formatDuration, formatMbps, formatMs, latencySplit } from './format'

const RECENT = 4

const props = defineProps({
  session: { type: Object, default: null },
  history: { type: Array, default: null },
  loading: { type: Boolean, default: false },
})
defineEmits(['history'])

const { t, locale } = useI18n()

const last = computed(() => props.history?.[0] ?? null)
const live = computed(() => Boolean(props.session))

const note = computed(() => {
  if (live.value) return t('nova.overview.health_live')
  if (last.value) return t('nova.overview.health_last', { when: relativeTime(last.value.started_at, locale.value) })
  return ''
})

const total = computed(() => {
  if (live.value) return formatMs(props.session.latency_ms?.total)
  if (last.value && Number.isFinite(last.value.avg_latency_ms)) return t('nova.overview.avg', { value: formatMs(last.value.avg_latency_ms) })
  return '—'
})

const split = computed(() => (live.value ? latencySplit(props.session.latency_ms) : null))
const splitLabel = computed(() => {
  const l = props.session?.latency_ms
  return l ? t('nova.overview.latency_split_label', { capture: formatMs(l.capture), encode: formatMs(l.encode), send: formatMs(l.send) }) : ''
})

const cells = computed(() => {
  if (live.value) {
    const s = props.session
    return [
      { key: 'loss', label: t('nova.overview.packet_loss'), value: Number.isFinite(s.loss_pct) ? `${s.loss_pct.toFixed(1)} %` : '—',
        hint: Number.isFinite(s.loss_pct) ? '' : t('nova.overview.no_loss_report') },
      { key: 'repeated', label: t('nova.overview.repeated_frames'), value: Number.isFinite(s.frames?.duplicated) ? String(s.frames.duplicated) : '—' },
      ...(s.abr ? [{ key: 'abr', label: t('nova.overview.adaptive_bitrate'), value: formatMbps(s.abr.current_kbps),
        hint: s.abr.last_reason || t('nova.overview.abr_range', { min: formatMbps(s.abr.min_kbps), max: formatMbps(s.abr.max_kbps) }) }] : []),
    ]
  }
  if (last.value) {
    return [
      { key: 'fps', label: t('nova.overview.avg_frame_rate'), value: Number.isFinite(last.value.avg_fps) ? `${Math.round(last.value.avg_fps)} fps` : '—' },
      { key: 'bitrate', label: t('nova.overview.avg_bitrate'), value: formatMbps(last.value.avg_bitrate_kbps) },
    ]
  }
  return []
})

const recent = computed(() => {
  const rows = []
  if (live.value) {
    const s = props.session
    rows.push({ key: `live-${s.id}`, app: s.app_name || t('nova.live.unknown_app'), device: s.client_name || t('nova.live.unknown_device'),
      when: t('nova.overview.now'), title: '', len: formatDuration(Date.now() / 1000 - (s.started_at || 0)), codec: s.codec || '—', live: true })
  }
  for (const h of (props.history || []).slice(0, RECENT - rows.length)) {
    rows.push({ key: `h-${h.id}`, app: h.app_name || t('nova.live.unknown_app'), device: h.client_name || t('nova.live.unknown_device'),
      when: relativeTime(h.started_at, locale.value), title: absoluteTime(h.started_at, locale.value),
      len: formatDuration(h.duration_s), codec: h.codec || '—', live: false })
  }
  return rows
})
</script>

<template>
  <NvCard :title="t('nova.overview.health_title')" flush class="nv-sh">
    <template #actions><span v-if="note" class="nv-sh__note">{{ note }}</span></template>

    <div v-if="loading" class="nv-sh__block" aria-busy="true">
      <span class="nv-visually-hidden">{{ t('nova.common.loading') }}</span><NvSkeleton :lines="3" />
    </div>

    <template v-else-if="live || last">
      <div class="nv-sh__block">
        <div class="nv-sh__total">
          <span class="nv-sh__label">{{ t('nova.overview.host_latency') }}</span>
          <span class="nv-mono nv-sh__ms">{{ total }}</span>
        </div>
        <template v-if="split">
          <div class="nv-sh__bar" role="img" :aria-label="splitLabel">
            <div class="nv-sh__seg nv-sh__seg--capture" :style="{ width: `${split.capture}%` }"></div>
            <div class="nv-sh__seg nv-sh__seg--encode" :style="{ width: `${split.encode}%` }"></div>
            <div class="nv-sh__seg nv-sh__seg--send" :style="{ width: `${split.send}%` }"></div>
          </div>
          <div class="nv-sh__legend" aria-hidden="true">
            <span><i class="nv-sh__key nv-sh__seg--capture"></i>{{ t('nova.overview.capture') }} <span class="nv-mono">{{ formatMs(session.latency_ms.capture) }}</span></span>
            <span><i class="nv-sh__key nv-sh__seg--encode"></i>{{ t('nova.overview.encode') }} <span class="nv-mono">{{ formatMs(session.latency_ms.encode) }}</span></span>
            <span><i class="nv-sh__key nv-sh__seg--send"></i>{{ t('nova.overview.send') }} <span class="nv-mono">{{ formatMs(session.latency_ms.send) }}</span></span>
          </div>
        </template>
        <span v-else class="nv-sh__hint">{{ t('nova.overview.split_while_live') }}</span>
      </div>

      <div v-if="cells.length" class="nv-sh__cells" :style="{ '--nv-sh-cells': cells.length }">
        <div v-for="c in cells" :key="c.key" class="nv-sh__cell">
          <span class="nv-sh__label">{{ c.label }}</span>
          <span class="nv-mono nv-sh__value">{{ c.value }}</span>
          <span v-if="c.hint" class="nv-sh__hint">{{ c.hint }}</span>
        </div>
      </div>
    </template>

    <div v-else class="nv-sh__block nv-sh__empty">
      <span class="nv-sh__label">{{ t('nova.overview.health_none') }}</span>
      <span class="nv-sh__hint">{{ t('nova.overview.health_none_desc') }}</span>
    </div>

    <div class="nv-sh__recent-head">
      <h3 class="nv-sh__h3">{{ t('nova.overview.recent_sessions') }}</h3>
      <button v-if="history?.length" type="button" class="nv-linkbtn" @click="$emit('history')">{{ t('nova.overview.history') }}</button>
    </div>
    <ul v-if="recent.length" class="nv-sh__recent">
      <li v-for="r in recent" :key="r.key" class="nv-sh__row">
        <span class="nv-sh__who">
          <span class="nv-sh__app">{{ r.app }}</span>
          <span class="nv-sh__sub" :title="r.title || null">{{ r.device }} · <span :class="{ 'nv-sh__now': r.live }">{{ r.when }}</span></span>
        </span>
        <span class="nv-mono nv-sh__len">{{ r.len }}</span>
        <span class="nv-mono nv-sh__codec">{{ r.codec }}</span>
      </li>
    </ul>
    <p v-else class="nv-sh__none">{{ t('nova.overview.no_sessions') }}</p>
  </NvCard>
</template>

<style>
@layer components {
  .nv-sh__note {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
  }

  .nv-sh__block {
    display: flex;
    flex-direction: column;
    gap: 10px;
    padding: var(--nv-space-4) var(--nv-space-5);
    border-bottom: 1px solid var(--nv-divider);
  }

  .nv-sh__total {
    display: flex;
    align-items: baseline;
    gap: var(--nv-space-2);
  }

  .nv-sh__label {
    flex-grow: 1;
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
  }

  .nv-sh__ms {
    font-size: var(--nv-text-2xl);
    font-weight: 500;
  }

  .nv-sh__bar {
    display: flex;
    gap: 2px;
    height: 8px;
    border-radius: 4px;
    overflow: hidden;
  }

  .nv-sh__seg--capture { background: var(--nv-accent-text); }
  .nv-sh__seg--encode { background: var(--nv-accent); }
  .nv-sh__seg--send { background: var(--nv-border-strong); }

  .nv-sh__legend {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-2) var(--nv-space-4);
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
  }

  .nv-sh__legend > span {
    display: flex;
    align-items: center;
    gap: 6px;
  }

  .nv-sh__key {
    display: inline-block;
    width: 8px;
    height: 8px;
    border-radius: 2px;
  }

  .nv-sh__hint {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
  }

  .nv-sh__cells {
    display: grid;
    grid-template-columns: repeat(var(--nv-sh-cells, 2), minmax(0, 1fr));
    border-bottom: 1px solid var(--nv-divider);
  }

  .nv-sh__cell {
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding: 14px var(--nv-space-5);
  }

  .nv-sh__cell + .nv-sh__cell {
    border-left: 1px solid var(--nv-divider);
  }

  .nv-sh__value {
    font-size: var(--nv-text-lg);
  }

  .nv-sh__recent-head {
    display: flex;
    align-items: center;
    min-height: 40px;
    padding: 0 var(--nv-space-5);
  }

  .nv-sh__h3 {
    flex-grow: 1;
    margin: 0;
    font-size: var(--nv-text-xs);
    font-weight: 500;
    color: var(--nv-text-muted);
  }

  .nv-linkbtn {
    min-height: 28px;
    padding: 0;
    border: 0;
    background: none;
    color: var(--nv-text-secondary);
    font: inherit;
    font-size: var(--nv-text-xs);
    cursor: pointer;
  }

  .nv-linkbtn:hover {
    color: var(--nv-text);
  }

  .nv-sh__recent {
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-sh__row {
    display: grid;
    grid-template-columns: minmax(0, 1fr) 72px 56px;
    align-items: center;
    column-gap: var(--nv-space-3);
    min-height: 48px;
    padding: 0 var(--nv-space-5);
    border-top: 1px solid var(--nv-divider);
    font-size: var(--nv-text-sm);
  }

  .nv-sh__who {
    display: flex;
    flex-direction: column;
    min-width: 0;
    line-height: 1.3;
  }

  .nv-sh__app {
    font-weight: 500;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-sh__sub {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-sh__now {
    color: var(--nv-success);
  }

  .nv-sh__len,
  .nv-sh__codec {
    text-align: right;
    color: var(--nv-text-secondary);
  }

  .nv-sh__none {
    margin: 0;
    padding: 0 var(--nv-space-5) var(--nv-space-4);
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }
}
</style>

<script setup>
/**
 * Tiny line chart (SVG) for a recent series, e.g. network bitrate over the last minute.
 * It is an image with a text alternative: always pass `label` describing what it shows and
 * the latest value ("Upload over the last minute, now 31 Mbps").
 *
 * Props: values (numbers, oldest first), label (required), width (px, 150), height (px, 30),
 *        tone ('accent' | 'success' | 'warning' | 'danger' | 'info'), min (y floor, default 0),
 *        fill (soft area under the line).
 */
import { computed } from 'vue'

const props = defineProps({
  values: { type: Array, default: () => [] },
  label: { type: String, required: true },
  width: { type: Number, default: 150 },
  height: { type: Number, default: 30 },
  tone: { type: String, default: 'accent' },
  min: { type: Number, default: 0 },
  fill: { type: Boolean, default: false },
})

const points = computed(() => {
  const vals = props.values.filter((v) => Number.isFinite(v))
  if (vals.length < 2) return []
  const max = Math.max(...vals, props.min + 1e-9)
  const lo = Math.min(props.min, ...vals)
  const pad = 2
  const h = props.height - pad * 2
  const step = props.width / (vals.length - 1)
  return vals.map((v, i) => [i * step, pad + h - ((v - lo) / (max - lo || 1)) * h])
})

const line = computed(() => points.value.map(([x, y], i) => `${i ? 'L' : 'M'}${x.toFixed(1)} ${y.toFixed(1)}`).join(' '))
const area = computed(() => (points.value.length
  ? `${line.value} L${props.width} ${props.height} L0 ${props.height} Z`
  : ''))
</script>

<template>
  <svg :class="['nv-spark', `nv-spark--${tone}`]" :width="width" :height="height" :viewBox="`0 0 ${width} ${height}`"
       preserveAspectRatio="none" role="img" :aria-label="label">
    <path v-if="fill && area" :d="area" class="nv-spark__area" />
    <path v-if="line" :d="line" class="nv-spark__line" vector-effect="non-scaling-stroke" />
    <line v-else x1="0" :y1="height / 2" :x2="width" :y2="height / 2" class="nv-spark__empty" />
  </svg>
</template>

<style>
@layer components {
  .nv-spark {
    display: block;
    flex-shrink: 0;
    overflow: visible;
    --nv-spark: var(--nv-accent-text);
  }

  .nv-spark--success {
    --nv-spark: var(--nv-success);
  }

  .nv-spark--warning {
    --nv-spark: var(--nv-warning);
  }

  .nv-spark--danger {
    --nv-spark: var(--nv-danger);
  }

  .nv-spark--info {
    --nv-spark: var(--nv-info);
  }

  .nv-spark__line {
    fill: none;
    stroke: var(--nv-spark);
    stroke-width: 2;
    stroke-linejoin: round;
    stroke-linecap: round;
  }

  .nv-spark__area {
    fill: var(--nv-spark);
    opacity: 0.12;
  }

  .nv-spark__empty {
    stroke: var(--nv-border-strong);
    stroke-width: 1;
    stroke-dasharray: 3 4;
  }
}
</style>

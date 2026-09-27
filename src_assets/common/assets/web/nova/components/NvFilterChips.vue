<script setup>
/**
 * Filter chips (SPEC §5): 32px pills; selected = accent-text border + accent-tint background;
 * optional count in mono. Single-select by default (radio-like toggle buttons), or `multiple`.
 *
 * v-model: selected value (single) or array of values (multiple).
 * Props: options — array of { value, label, count? } or strings; label (group aria-label, required),
 *        multiple, size ('md' | 'sm').
 */
import { computed } from 'vue'

const model = defineModel({ type: [String, Number, Array, Boolean, null], default: null })
const props = defineProps({
  options: { type: Array, required: true },
  label: { type: String, required: true },
  multiple: { type: Boolean, default: false },
  size: { type: String, default: 'md' },
})

const normalized = computed(() => props.options.map((o) => (typeof o === 'object' ? o : { value: o, label: String(o) })))

function isOn(value) {
  return props.multiple ? (model.value || []).includes(value) : model.value === value
}

function toggle(value) {
  if (!props.multiple) {
    model.value = value
    return
  }
  const current = new Set(model.value || [])
  if (current.has(value)) current.delete(value)
  else current.add(value)
  model.value = normalized.value.map((o) => o.value).filter((v) => current.has(v))
}
</script>

<template>
  <div role="group" :aria-label="label" :class="['nv-chips', `nv-chips--${size}`]">
    <button v-for="o in normalized" :key="String(o.value)" type="button"
            :class="['nv-chip', { 'nv-chip--on': isOn(o.value) }]" :aria-pressed="isOn(o.value) ? 'true' : 'false'"
            @click="toggle(o.value)">
      <span>{{ o.label }}</span>
      <span v-if="o.count !== undefined && o.count !== null" class="nv-chip__count">{{ o.count }}</span>
    </button>
  </div>
</template>

<style>
@layer components {
  .nv-chips {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-2);
  }

  .nv-chip {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    min-height: 32px;
    padding: 0 var(--nv-space-3);
    border-radius: var(--nv-radius-pill);
    border: 1px solid var(--nv-chip-border);
    background: transparent;
    color: var(--nv-text-secondary);
    font: inherit;
    font-size: var(--nv-text-sm);
    font-weight: 500;
    white-space: nowrap;
    cursor: pointer;
    transition: background-color 150ms ease-out, border-color 150ms ease-out, color 150ms ease-out;
  }

  .nv-chip:hover:not(.nv-chip--on) {
    background: var(--nv-raised);
    color: var(--nv-text);
  }

  .nv-chip--on {
    border-color: var(--nv-accent-text);
    background: var(--nv-accent-tint);
    color: var(--nv-accent-text);
  }

  .nv-chip__count {
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    font-variant-numeric: tabular-nums;
    color: var(--nv-text-muted);
  }

  .nv-chip--on .nv-chip__count {
    color: var(--nv-accent-text);
  }

  .nv-chips--sm .nv-chip {
    min-height: 28px;
    padding: 0 10px;
    font-size: var(--nv-text-xs);
  }

  @media (max-width: 767px) {
    .nv-chip {
      min-height: 40px;
    }
  }
}
</style>

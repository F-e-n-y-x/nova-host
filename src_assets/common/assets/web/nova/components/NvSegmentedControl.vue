<script setup>
/**
 * Segmented control for 2–4 mutually exclusive choices. Each option is a toggle button
 * (aria-pressed); Left/Right/Home/End move the selection like a radio group.
 *
 * v-model: the selected value.
 * Props: label (group aria-label; required unless labelledby is set), labelledby,
 *        options (array of strings or { value, label }), size ('md' | 'sm'), disabled.
 */
import { computed, ref } from 'vue'

const model = defineModel({ type: [String, Number, Boolean], default: '' })
const props = defineProps({
  label: { type: String, default: '' },
  labelledby: { type: String, default: null },
  options: { type: Array, required: true },
  size: { type: String, default: 'md' },
  disabled: { type: Boolean, default: false },
})

const buttons = ref([])
const normalized = computed(() => props.options.map((o) => (typeof o === 'object' ? o : { value: o, label: String(o) })))

function select(index) {
  const option = normalized.value[index]
  if (!option) return
  model.value = option.value
  buttons.value[index]?.focus()
}

function onKeydown(event, index) {
  const last = normalized.value.length - 1
  const moves = { ArrowRight: index + 1, ArrowDown: index + 1, ArrowLeft: index - 1, ArrowUp: index - 1, Home: 0, End: last }
  if (!(event.key in moves)) return
  event.preventDefault()
  let next = moves[event.key]
  if (next > last) next = 0
  if (next < 0) next = last
  select(next)
}
</script>

<template>
  <div role="group" :aria-label="labelledby ? null : label" :aria-labelledby="labelledby"
       :class="['nv-seg', `nv-seg--${size}`]">
    <button v-for="(o, i) in normalized" :key="String(o.value)" ref="buttons" type="button"
            :class="['nv-seg__option', { 'nv-seg__option--on': o.value === model }]"
            :aria-pressed="o.value === model ? 'true' : 'false'" :disabled="disabled"
            :tabindex="o.value === model || (i === 0 && !normalized.some((x) => x.value === model)) ? 0 : -1"
            @click="select(i)" @keydown="onKeydown($event, i)">{{ o.label }}</button>
  </div>
</template>

<style>
@layer components {
  .nv-seg {
    display: inline-flex;
    flex-wrap: wrap;
    gap: 2px;
    padding: 3px;
    border-radius: 9px;
    border: 1px solid var(--nv-border-strong);
    background: var(--nv-raised);
  }

  .nv-seg__option {
    min-height: 34px;
    padding: 0 var(--nv-space-4);
    border-radius: var(--nv-radius-sm);
    border: 0;
    background: transparent;
    color: var(--nv-text-secondary);
    font: inherit;
    font-size: var(--nv-text-sm);
    font-weight: 500;
    cursor: pointer;
  }

  .nv-seg--sm .nv-seg__option {
    min-height: 30px;
    padding: 0 var(--nv-space-3);
  }

  .nv-seg__option:hover:not(:disabled):not(.nv-seg__option--on) {
    color: var(--nv-text);
  }

  .nv-seg__option--on {
    background: var(--nv-accent);
    color: var(--nv-on-accent);
  }

  .nv-seg__option:disabled {
    opacity: 0.55;
    cursor: not-allowed;
  }
}
</style>

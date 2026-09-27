<script setup>
/**
 * Checkbox with a visible label (native input, so it works with forms, Space and screen readers).
 *
 * v-model: boolean.
 * Props: label (required unless hideLabel with ariaLabel), description, indeterminate, disabled,
 *        hideLabel, id.
 */
import { computed, ref, useId, watchEffect } from 'vue'

const model = defineModel({ type: Boolean, default: false })
const props = defineProps({
  label: { type: String, required: true },
  description: { type: String, default: '' },
  indeterminate: { type: Boolean, default: false },
  disabled: { type: Boolean, default: false },
  hideLabel: { type: Boolean, default: false },
  id: { type: String, default: '' },
})

const autoId = useId()
const input = ref(null)
const inputId = computed(() => props.id || `nv-check-${autoId}`)

watchEffect(() => {
  if (input.value) input.value.indeterminate = props.indeterminate
})

defineExpose({ focus: () => input.value?.focus() })
</script>

<template>
  <span :class="['nv-check', { 'nv-check--disabled': disabled }]">
    <input :id="inputId" ref="input" v-model="model" type="checkbox" class="nv-check__input" :disabled="disabled"
           :aria-describedby="description ? `${inputId}-desc` : null">
    <span class="nv-check__text">
      <label :for="inputId" :class="['nv-check__label', { 'nv-visually-hidden': hideLabel }]">{{ label }}</label>
      <span v-if="description" :id="`${inputId}-desc`" class="nv-check__desc">{{ description }}</span>
    </span>
  </span>
</template>

<style>
@layer components {
  .nv-check {
    display: inline-flex;
    align-items: flex-start;
    gap: 10px;
  }

  .nv-check__input {
    appearance: none;
    position: relative;
    width: 18px;
    height: 18px;
    flex-shrink: 0;
    margin: 1px 0 0;
    border: 1px solid var(--nv-border-strong);
    border-radius: 5px;
    background: var(--nv-bg);
    cursor: pointer;
    transition: background-color 150ms ease-out, border-color 150ms ease-out;
  }

  .nv-check__input:checked,
  .nv-check__input:indeterminate {
    background: var(--nv-accent);
    border-color: var(--nv-accent);
  }

  .nv-check__input:checked::after {
    content: "";
    position: absolute;
    left: 5px;
    top: 1px;
    width: 5px;
    height: 10px;
    border: solid #FFFFFF;
    border-width: 0 2px 2px 0;
    transform: rotate(45deg);
  }

  .nv-check__input:indeterminate::after {
    content: "";
    position: absolute;
    left: 3px;
    top: 7px;
    width: 10px;
    height: 2px;
    background: #FFFFFF;
  }

  .nv-check__input:disabled {
    cursor: not-allowed;
    opacity: 0.5;
  }

  .nv-check__text {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .nv-check__label {
    font-size: var(--nv-text-md);
    color: var(--nv-text);
    cursor: pointer;
  }

  .nv-check--disabled .nv-check__label {
    cursor: not-allowed;
    color: var(--nv-text-muted);
  }

  .nv-check__desc {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  @media (max-width: 767px) {
    .nv-check__input {
      width: 22px;
      height: 22px;
    }

    .nv-check__input:checked::after {
      left: 7px;
      top: 2px;
      width: 6px;
      height: 12px;
    }
  }
}
</style>

<script setup>
/**
 * Labelled text input with optional hint and error text (both linked via aria-describedby).
 *
 * v-model: string.
 * Props: label (required; use hideLabel to show it to screen readers only), hint, error,
 *        type ('text' | 'password' | 'search' | 'url' | …), placeholder, id, disabled,
 *        required, autocomplete, mono (monospace value), hideLabel.
 */
import { computed, useId } from 'vue'

const model = defineModel({ type: String, default: '' })
const props = defineProps({
  label: { type: String, required: true },
  hint: { type: String, default: '' },
  error: { type: String, default: '' },
  type: { type: String, default: 'text' },
  placeholder: { type: String, default: '' },
  id: { type: String, default: '' },
  disabled: { type: Boolean, default: false },
  required: { type: Boolean, default: false },
  autocomplete: { type: String, default: null },
  mono: { type: Boolean, default: false },
  hideLabel: { type: Boolean, default: false },
})

const autoId = useId()
const inputId = computed(() => props.id || `nv-text-${autoId}`)
const hintId = computed(() => `${inputId.value}-hint`)
const errorId = computed(() => `${inputId.value}-error`)
const describedBy = computed(() => [props.hint && hintId.value, props.error && errorId.value].filter(Boolean).join(' ') || null)
</script>

<template>
  <div class="nv-field">
    <label :for="inputId" :class="['nv-field__label', { 'nv-visually-hidden': hideLabel }]">{{ label }}</label>
    <input :id="inputId" v-model="model" :type="type" :placeholder="placeholder" :disabled="disabled"
           :required="required" :autocomplete="autocomplete" :aria-describedby="describedBy"
           :aria-invalid="error ? 'true' : null" :class="['nv-input', { 'nv-mono': mono, 'nv-input--invalid': error }]">
    <p v-if="hint" :id="hintId" class="nv-field__hint">{{ hint }}</p>
    <p v-if="error" :id="errorId" class="nv-field__error">{{ error }}</p>
  </div>
</template>

<style>
@layer components {
  .nv-field {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
    min-width: 0;
  }

  .nv-field__label {
    font-weight: 500;
    color: var(--nv-text);
  }

  .nv-field__hint {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-field__error {
    font-size: var(--nv-text-sm);
    color: var(--nv-danger);
    font-weight: 500;
  }

  .nv-input {
    min-height: var(--nv-control-height);
    width: 100%;
    padding: 0 var(--nv-space-3);
    border-radius: var(--nv-radius-md);
    border: 1px solid var(--nv-border-strong);
    background: var(--nv-surface);
    color: var(--nv-text);
    font: inherit;
  }

  .nv-input::placeholder {
    color: var(--nv-text-muted);
  }

  .nv-input:disabled {
    opacity: 0.55;
    cursor: not-allowed;
  }

  .nv-input--invalid {
    border-color: var(--nv-danger);
  }

  select.nv-input {
    padding-right: var(--nv-space-10);
    appearance: none;
    cursor: pointer;
  }
}
</style>

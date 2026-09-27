<script setup>
/**
 * Labelled number input with an optional unit suffix (e.g. "fps", "Mbps").
 *
 * v-model: number (null when the field is empty).
 * Props: label (required), hideLabel, unit, min, max, step, hint, error, id, disabled.
 */
import { computed, useId } from 'vue'

const model = defineModel({ type: Number, default: null })
const props = defineProps({
  label: { type: String, required: true },
  hideLabel: { type: Boolean, default: false },
  unit: { type: String, default: '' },
  min: { type: Number, default: null },
  max: { type: Number, default: null },
  step: { type: Number, default: 1 },
  hint: { type: String, default: '' },
  error: { type: String, default: '' },
  id: { type: String, default: '' },
  disabled: { type: Boolean, default: false },
})

const autoId = useId()
const inputId = computed(() => props.id || `nv-number-${autoId}`)
const describedBy = computed(() => [props.unit && `${inputId.value}-unit`, props.hint && `${inputId.value}-hint`,
  props.error && `${inputId.value}-error`].filter(Boolean).join(' ') || null)

function onInput(event) {
  const raw = event.target.value
  model.value = raw === '' ? null : Number(raw)
}
</script>

<template>
  <div class="nv-field">
    <label :for="inputId" :class="['nv-field__label', { 'nv-visually-hidden': hideLabel }]">{{ label }}</label>
    <div class="nv-number">
      <input :id="inputId" type="number" inputmode="decimal" :value="model ?? ''" :min="min" :max="max" :step="step"
             :disabled="disabled" :aria-describedby="describedBy" :aria-invalid="error ? 'true' : null"
             :class="['nv-input', 'nv-mono', 'nv-number__input', { 'nv-input--invalid': error }]" @input="onInput">
      <span v-if="unit" :id="`${inputId}-unit`" class="nv-number__unit">{{ unit }}</span>
    </div>
    <p v-if="hint" :id="`${inputId}-hint`" class="nv-field__hint">{{ hint }}</p>
    <p v-if="error" :id="`${inputId}-error`" class="nv-field__error">{{ error }}</p>
  </div>
</template>

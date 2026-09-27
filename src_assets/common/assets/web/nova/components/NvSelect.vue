<script setup>
/**
 * Labelled native <select>.
 *
 * v-model: the selected option's value.
 * Props: label (required), hideLabel, options (array of strings or { value, label, disabled }),
 *        hint, error, id, disabled.
 */
import { computed, useId } from 'vue'
import { ChevronDown } from '@lucide/vue'

const model = defineModel({ type: [String, Number, Boolean], default: '' })
const props = defineProps({
  label: { type: String, required: true },
  hideLabel: { type: Boolean, default: false },
  options: { type: Array, required: true },
  hint: { type: String, default: '' },
  error: { type: String, default: '' },
  id: { type: String, default: '' },
  disabled: { type: Boolean, default: false },
})

const autoId = useId()
const selectId = computed(() => props.id || `nv-select-${autoId}`)
const normalized = computed(() => props.options.map((o) => (typeof o === 'object' ? o : { value: o, label: String(o) })))
const describedBy = computed(() => [props.hint && `${selectId.value}-hint`, props.error && `${selectId.value}-error`]
  .filter(Boolean).join(' ') || null)
</script>

<template>
  <div class="nv-field">
    <label :for="selectId" :class="['nv-field__label', { 'nv-visually-hidden': hideLabel }]">{{ label }}</label>
    <div class="nv-select">
      <select :id="selectId" v-model="model" :disabled="disabled" :aria-describedby="describedBy"
              :aria-invalid="error ? 'true' : null" :class="['nv-input', { 'nv-input--invalid': error }]">
        <option v-for="o in normalized" :key="String(o.value)" :value="o.value" :disabled="o.disabled">{{ o.label }}</option>
      </select>
      <ChevronDown class="nv-select__icon" :size="16" aria-hidden="true" />
    </div>
    <p v-if="hint" :id="`${selectId}-hint`" class="nv-field__hint">{{ hint }}</p>
    <p v-if="error" :id="`${selectId}-error`" class="nv-field__error">{{ error }}</p>
  </div>
</template>

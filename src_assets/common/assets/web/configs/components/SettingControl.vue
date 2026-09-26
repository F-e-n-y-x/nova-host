<script setup>
/**
 * The input for one simple setting (switch, segmented control, select, number, text or
 * path), chosen from the option's schema entry.
 *
 * v-model: the option's raw value (keeps the config file's own spelling, e.g. "enabled").
 * Props: optionKey, label (accessible name), labelledby, describedby, choices, placeholder,
 *        error (translated), platform.
 */
import { computed } from 'vue'
import NvNumberField from '../../nova/components/NvNumberField.vue'
import NvSegmentedControl from '../../nova/components/NvSegmentedControl.vue'
import NvSelect from '../../nova/components/NvSelect.vue'
import NvSwitch from '../../nova/components/NvSwitch.vue'
import NvTextField from '../../nova/components/NvTextField.vue'
import PathField from './PathField.vue'
import { OPTIONS, isOn } from '../settings_schema.js'
import { DEFAULTS, boolRepresentation } from '../settings_model.js'

const model = defineModel({ type: [String, Number, Boolean, Object, Array], default: '' })
const props = defineProps({
  optionKey: { type: String, required: true },
  label: { type: String, required: true },
  labelledby: { type: String, default: null },
  describedby: { type: String, default: null },
  choices: { type: Array, default: () => [] },
  placeholder: { type: String, default: '' },
  error: { type: String, default: '' },
})

const option = computed(() => OPTIONS[props.optionKey] || {})
const SEGMENT_LABEL_LIMIT = 16

/** Segmented control only for a few short choices; long labels read better in a select. */
const useSegments = computed(() => props.choices.length > 1 && props.choices.length <= 4 &&
  props.choices.every((c) => c.label.length <= SEGMENT_LABEL_LIMIT && !c.disabled))

const boolValue = computed({
  get: () => isOn(typeof model.value === 'string' ? model.value.toLowerCase() : model.value),
  set: (checked) => {
    const [onValue, offValue] = boolRepresentation(model.value, DEFAULTS[props.optionKey])
    model.value = checked ? onValue : offValue
  },
})

const choiceValue = computed({
  get: () => String(model.value ?? ''),
  set: (value) => { model.value = value },
})

const numberValue = computed({
  get: () => {
    if (model.value === '' || model.value === null || model.value === undefined) return null
    const n = Number(model.value)
    return Number.isNaN(n) ? null : n
  },
  set: (value) => { model.value = value ?? '' },
})

const textValue = computed({
  get: () => String(model.value ?? ''),
  set: (value) => { model.value = value },
})
</script>

<template>
  <div :class="['nv-cfg-control', `nv-cfg-control--${option.type}`]">
    <NvSwitch v-if="option.type === 'bool'" v-model="boolValue" :labelledby="labelledby" :describedby="describedby"
              show-state />
    <template v-else-if="option.type === 'choice'">
      <NvSegmentedControl v-if="useSegments" v-model="choiceValue" :labelledby="labelledby" :options="choices" />
      <NvSelect v-else v-model="choiceValue" :label="label" hide-label :options="choices" :error="error" />
    </template>
    <NvNumberField v-else-if="option.type === 'number'" v-model="numberValue" :label="label" hide-label
                   :unit="option.unit || ''" :min="option.min ?? null" :max="option.max ?? null"
                   :step="option.step ?? 1" :error="error" />
    <PathField v-else-if="option.type === 'path'" v-model="textValue" :label="label" :placeholder="placeholder"
               :error="error" />
    <NvTextField v-else v-model="textValue" :label="label" hide-label :placeholder="placeholder"
                 :mono="!!option.mono" :error="error" />
  </div>
</template>

<style>
@layer components {
  .nv-cfg-control--text,
  .nv-cfg-control--path {
    width: 320px;
  }

  .nv-cfg-control--choice .nv-field {
    min-width: 240px;
    max-width: 360px;
  }

  @media (max-width: 699px) {
    .nv-cfg-control--text,
    .nv-cfg-control--path,
    .nv-cfg-control--choice,
    .nv-cfg-control--choice .nv-field {
      width: 100%;
      max-width: none;
      min-width: 0;
    }
  }
}
</style>

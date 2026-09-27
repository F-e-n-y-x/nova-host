<script setup>
/**
 * The input for one simple setting (switch, segmented control, select, number, text,
 * secret or path), chosen from the option's schema entry. Options with a `picker` show the
 * host's list (displays, audio sinks) as a select when the host provides it.
 *
 * v-model: the option's raw value (keeps the config file's own spelling, e.g. "enabled").
 * Props: optionKey, label (accessible name), labelledby, describedby, choices, placeholder,
 *        error (translated), platform.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import NvNumberField from '../../nova/components/NvNumberField.vue'
import NvSegmentedControl from '../../nova/components/NvSegmentedControl.vue'
import NvSelect from '../../nova/components/NvSelect.vue'
import NvSwitch from '../../nova/components/NvSwitch.vue'
import NvTextField from '../../nova/components/NvTextField.vue'
import PathField from './PathField.vue'
import { OPTIONS, isOn } from '../settings_schema.js'
import { DEFAULTS, SECRET_MASK, boolRepresentation } from '../settings_model.js'
import { pickerChoices, useHostProbes } from '../hostProbes.js'

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
const { t } = useI18n()
const probes = useHostProbes()

/** Host-provided choices for picker options; null means "type it in". */
const pickerOptions = computed(() => {
  if (!option.value.picker) return null
  // Read the refs so the select appears once the probe answers.
  void probes.displays.value
  void probes.sinks.value
  return pickerChoices(option.value.picker, model.value, t)
})
const kind = computed(() => (pickerOptions.value ? 'picker' : option.value.type))
const secretStored = computed(() => option.value.type === 'secret' && model.value === SECRET_MASK)
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
  <div :class="['nv-cfg-control', `nv-cfg-control--${kind}`]">
    <NvSwitch v-if="option.type === 'bool'" v-model="boolValue" :labelledby="labelledby" :describedby="describedby"
              show-state />
    <template v-else-if="option.type === 'choice'">
      <NvSegmentedControl v-if="useSegments" v-model="choiceValue" :labelledby="labelledby" :options="choices" />
      <NvSelect v-else v-model="choiceValue" :label="label" hide-label :options="choices" :error="error" />
    </template>
    <NvNumberField v-else-if="option.type === 'number'" v-model="numberValue" :label="label" hide-label
                   :unit="option.unit || ''" :min="option.min ?? null" :max="option.max ?? null"
                   :step="option.step ?? 1" :error="error" />
    <NvSelect v-else-if="kind === 'picker'" v-model="textValue" :label="label" hide-label :options="pickerOptions"
              :error="error" />
    <template v-else-if="option.type === 'secret'">
      <NvTextField v-model="textValue" type="password" :label="label" hide-label :placeholder="placeholder" mono
                   autocomplete="off" spellcheck="false" :error="error" />
      <p v-if="secretStored" class="nv-cfg-control__note">{{ t('nova.settings.secret_stored') }}</p>
    </template>
    <PathField v-else-if="option.type === 'path'" v-model="textValue" :label="label" :placeholder="placeholder"
               :error="error" />
    <NvTextField v-else v-model="textValue" :label="label" hide-label :placeholder="placeholder"
                 :mono="!!option.mono" :error="error" />
  </div>
</template>

<style>
@layer components {
  .nv-cfg-control--text,
  .nv-cfg-control--secret,
  .nv-cfg-control--path {
    width: 320px;
  }

  .nv-cfg-control--picker .nv-field {
    min-width: 240px;
    max-width: 360px;
  }

  .nv-cfg-control__note {
    margin: var(--nv-space-1) 0 0;
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
  }

  .nv-cfg-control--choice .nv-field {
    min-width: 240px;
    max-width: 360px;
  }

  @media (max-width: 699px) {
    .nv-cfg-control--text,
    .nv-cfg-control--secret,
    .nv-cfg-control--picker,
    .nv-cfg-control--picker .nv-field,
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

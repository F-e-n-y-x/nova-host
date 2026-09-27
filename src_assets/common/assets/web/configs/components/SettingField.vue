<script setup>
/**
 * One option on the Settings page: its row (label, short description, "More" help,
 * Changed/Reset) with the matching control or list editor.
 *
 * Props: optionKey, value, config (working copy; some choices and editors read other
 *        options), platform, error ({ key, params } or null), highlight (search match).
 * Emits: update(key, value), reset(key), touch(key) (focus left the setting; its error may show).
 */
import { computed, toRef } from 'vue'
import ArtSourcesEditor from './ArtSourcesEditor.vue'
import KeybindingsEditor from './KeybindingsEditor.vue'
import ModeRemappingEditor from './ModeRemappingEditor.vue'
import PrepCommandsEditor from './PrepCommandsEditor.vue'
import SettingControl from './SettingControl.vue'
import SettingHelp from './SettingHelp.vue'
import SettingRow from './SettingRow.vue'
import { OPTIONS } from '../settings_schema.js'
import { isModified, splitDescription } from '../settings_model.js'
import { useOptionText } from '../useOptionText.js'

const props = defineProps({
  optionKey: { type: String, required: true },
  value: { type: [String, Number, Boolean, Object, Array], default: '' },
  config: { type: Object, required: true },
  platform: { type: String, required: true },
  error: { type: Object, default: null },
})
const emit = defineEmits(['update', 'reset', 'touch'])

const EDITORS = { PrepCommandsEditor, KeybindingsEditor, ModeRemappingEditor, ArtSourcesEditor }

const text = useOptionText(toRef(props, 'platform'))
const option = computed(() => OPTIONS[props.optionKey] || {})
const editor = computed(() => EDITORS[option.value.type] || null)
/** Only the props each editor declares. */
const editorProps = computed(() => {
  if (option.value.type === 'PrepCommandsEditor') return { platform: props.platform }
  if (option.value.type === 'ModeRemappingEditor' || option.value.type === 'ArtSourcesEditor') return { config: props.config }
  return {}
})
const label = computed(() => text.label(props.optionKey))

const paragraphs = computed(() => text.description(props.optionKey))
const lead = computed(() => splitDescription(paragraphs.value[0] || ''))
const more = computed(() => [lead.value.rest, ...paragraphs.value.slice(1)].filter(Boolean))

const modified = computed(() => isModified(props.optionKey, props.value))
const choices = computed(() => (option.value.type === 'choice' ? text.choices(props.optionKey, props.config) : []))
const warning = computed(() => {
  const key = option.value.warn?.(props.config, props.platform)
  return key ? text.t(key) : ''
})
const errorText = computed(() => (props.error ? text.t(props.error.key, props.error.params || {}) : ''))

function onFocusOut(event) {
  // Only when focus leaves the whole setting, not when it moves between its own parts.
  if (!event.currentTarget.contains(event.relatedTarget)) emit('touch', props.optionKey)
}

function update(value) {
  emit('update', props.optionKey, value)
}
</script>

<template>
  <SettingRow :id="optionKey" :label="label" :description="lead.lead" :more="more" :has-more-slot="!!option.help"
              :more-open="!!option.helpOpen" :modified="modified" :warning="warning"
              :error="editor ? errorText : ''" @reset="emit('reset', optionKey)" @focusout="onFocusOut">
    <template v-if="!editor" #default="{ labelId, descriptionId }">
      <SettingControl :model-value="value" :option-key="optionKey" :label="label" :labelledby="labelId"
                      :describedby="descriptionId" :choices="choices" :placeholder="text.placeholder(optionKey)"
                      :error="errorText" @update:model-value="update" />
    </template>
    <template v-if="option.help" #more>
      <SettingHelp :topic="option.help" :platform="platform" :config="config" />
    </template>
    <template v-if="editor" #details>
      <component :is="editor" :model-value="value" v-bind="editorProps" @update:model-value="update" />
    </template>
  </SettingRow>
</template>

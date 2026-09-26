<script setup>
/**
 * Picker for one Windows virtual-key code ("0x10 (Shift)"). Codes typed into the config
 * file by hand that are not in the list stay selectable as a custom entry.
 *
 * v-model: code string ("0x10").
 * Props: label (accessible name).
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import NvSelect from '../nova/components/NvSelect.vue'
import {
  getVirtualKeyCodeDescription,
  hasVirtualKeyCodeOption,
  isValidVirtualKeyCode,
  virtualKeyCodes,
} from './virtual_key_codes.js'

const model = defineModel({ type: String, required: true })
defineProps({
  label: { type: String, required: true },
})
const { t } = useI18n()

const options = computed(() => {
  const list = [{ value: '', label: t('config.keybindings_select'), disabled: true }]
  if (model.value && !hasVirtualKeyCodeOption(model.value)) {
    list.push({
      value: model.value,
      label: `${model.value} (${getVirtualKeyCodeDescription(model.value) ?? t('config.keybindings_custom')})`,
    })
  }
  for (const keyCode of virtualKeyCodes) {
    list.push({ value: keyCode.code, label: `${keyCode.code} (${keyCode.description})` })
  }
  return list
})
const error = computed(() => (isValidVirtualKeyCode(model.value) ? '' : t('config.keybindings_invalid')))
</script>

<template>
  <NvSelect v-model="model" :label="label" hide-label :options="options" :error="error" />
</template>

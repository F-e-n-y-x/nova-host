<script setup>
/**
 * Edit what a device may do: pick a preset, or switch individual permissions.
 * Emits `change(update, next)`: `update` is what to send to the host, `next` the full
 * permission object to show while it saves.
 */
import { computed, useId } from 'vue'
import { useI18n } from 'vue-i18n'
import NvSegmentedControl from '../components/NvSegmentedControl.vue'
import NvSettingRow from '../components/NvSettingRow.vue'
import NvSwitch from '../components/NvSwitch.vue'
import { PERMISSION_FLAGS, PERMISSION_PRESETS, permissionPreset } from './format'

/** Flags each preset turns on; the rest are off. */
const PRESET_FLAGS = {
  full: PERMISSION_FLAGS,
  play: ['input_keyboard', 'input_mouse', 'input_controller', 'input_touch_pen', 'launch_apps'],
  view_only: [],
}

const props = defineProps({
  permissions: { type: Object, default: null },
  deviceName: { type: String, required: true },
  disabled: { type: Boolean, default: false },
})
const emit = defineEmits(['change'])

const { t } = useI18n()
const headingId = useId()

const current = computed(() => {
  const source = props.permissions || {}
  return Object.fromEntries(PERMISSION_FLAGS.map((flag) => [flag, source[flag] !== false]))
})
const preset = computed(() => permissionPreset(props.permissions))
const presetOptions = computed(() => PERMISSION_PRESETS.map((value) => ({ value, label: t(`nova.devices.preset_${value}`) })))

/**
 * Work out which preset a set of flags matches.
 *
 * @param {object} flags Flag name → boolean.
 * @returns {string} Preset name or 'custom'.
 */
function presetFor(flags) {
  const on = PERMISSION_FLAGS.filter((f) => flags[f])
  const match = PERMISSION_PRESETS.find((p) => PRESET_FLAGS[p].length === on.length && PRESET_FLAGS[p].every((f) => flags[f]))
  return match || 'custom'
}

/**
 * Apply a preset.
 *
 * @param {string} value Preset name.
 */
function choosePreset(value) {
  if (value === preset.value) return
  const flags = Object.fromEntries(PERMISSION_FLAGS.map((f) => [f, PRESET_FLAGS[value].includes(f)]))
  emit('change', { preset: value }, { ...flags, preset: value })
}

/**
 * Switch one permission.
 *
 * @param {string} flag Flag name.
 * @param {boolean} value New value.
 */
function setFlag(flag, value) {
  const flags = { ...current.value, [flag]: value }
  emit('change', { [flag]: value }, { ...flags, preset: presetFor(flags) })
}
</script>

<template>
  <section class="nv-perm" :aria-labelledby="headingId">
    <h3 :id="headingId" class="nv-perm__title">{{ t('nova.devices.permissions_title') }}</h3>
    <p class="nv-perm__intro">{{ t('nova.devices.permissions_intro', { name: deviceName }) }}</p>
    <div class="nv-perm__presets">
      <NvSegmentedControl :model-value="preset" :options="presetOptions" :label="t('nova.devices.preset_label')"
                          :disabled="disabled" @update:model-value="choosePreset" />
      <p class="nv-perm__preset-desc" aria-live="polite">{{ t(`nova.devices.preset_${preset}_desc`) }}</p>
    </div>
    <div class="nv-perm__flags">
      <NvSettingRow v-for="flag in PERMISSION_FLAGS" :key="flag" :label="t(`nova.devices.perm_${flag}`)"
                    :description="t(`nova.devices.perm_${flag}_desc`)">
        <template #default="{ labelId, descriptionId }">
          <NvSwitch :model-value="current[flag]" :labelledby="labelId" :describedby="descriptionId"
                    :disabled="disabled" @update:model-value="(value) => setFlag(flag, value)" />
        </template>
      </NvSettingRow>
    </div>
  </section>
</template>

<style scoped>
@layer components {
  .nv-perm {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
  }

  .nv-perm__title {
    margin: 0;
    font-size: var(--nv-text-md);
    font-weight: 600;
  }

  .nv-perm__intro,
  .nv-perm__preset-desc {
    margin: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-perm__presets {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
  }
}
</style>

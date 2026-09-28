<script setup>
/**
 * Access for one device: a preset segmented control over the permission switches. The flags
 * that control the PC itself (sleep, host commands) sit in their own group below the stream ones.
 * Controlled: v-model is the flag object (flag name → boolean); picking a preset sets the flags,
 * and the preset shown is whatever the flags match ("Custom" when none does).
 */
import { computed, useId } from 'vue'
import { useI18n } from 'vue-i18n'
import NvSegmentedControl from '../components/NvSegmentedControl.vue'
import NvSwitch from '../components/NvSwitch.vue'
import { HOST_CONTROL_FLAGS, PERMISSION_FLAGS, PERMISSION_PRESETS, flagsForPreset, presetForFlags } from './format'

const flags = defineModel({ type: Object, required: true })
const props = defineProps({
  disabled: { type: Boolean, default: false },
  /** Names of the host commands defined on this PC; null while unknown. */
  commandNames: { type: Array, default: null },
})

const streamFlags = PERMISSION_FLAGS.filter((f) => !HOST_CONTROL_FLAGS.includes(f))
const commandsHint = computed(() => {
  if (!props.commandNames) return ''
  return props.commandNames.length
    ? t('nova.devices.perm_host_commands_list', { names: props.commandNames.join(', ') })
    : t('nova.devices.perm_host_commands_none')
})

const { t } = useI18n()
const id = useId()

const preset = computed(() => presetForFlags(flags.value))
const presetOptions = computed(() => PERMISSION_PRESETS.map((value) => ({ value, label: t(`nova.devices.preset_${value}`) })))

/**
 * Apply a preset.
 *
 * @param {string} value Preset name.
 */
function choosePreset(value) {
  if (value !== preset.value) flags.value = flagsForPreset(value)
}

/**
 * Switch one permission.
 *
 * @param {string} flag Flag name.
 * @param {boolean} value New value.
 */
function setFlag(flag, value) {
  flags.value = { ...flags.value, [flag]: value }
}
</script>

<template>
  <div class="nv-perm">
    <NvSegmentedControl :model-value="preset" :options="presetOptions" :label="t('nova.devices.preset_label')"
                        :disabled="disabled" class="nv-perm__presets" @update:model-value="choosePreset" />
    <p class="nv-perm__preset-desc" aria-live="polite">{{ t(`nova.devices.preset_${preset}_desc`) }}</p>
    <ul class="nv-perm__flags">
      <li v-for="flag in streamFlags" :key="flag" class="nv-perm__flag">
        <span class="nv-perm__text">
          <span :id="`${id}-${flag}`" class="nv-perm__label">{{ t(`nova.devices.perm_${flag}`) }}</span>
          <span :id="`${id}-${flag}-desc`" class="nv-perm__desc">{{ t(`nova.devices.perm_${flag}_desc`) }}</span>
        </span>
        <NvSwitch :model-value="flags[flag]" :labelledby="`${id}-${flag}`" :describedby="`${id}-${flag}-desc`"
                  :disabled="disabled" @update:model-value="(value) => setFlag(flag, value)" />
      </li>
    </ul>
    <h3 :id="`${id}-host`" class="nv-perm__group">{{ t('nova.devices.perm_group_host') }}</h3>
    <ul class="nv-perm__flags" :aria-labelledby="`${id}-host`">
      <li v-for="flag in HOST_CONTROL_FLAGS" :key="flag" class="nv-perm__flag">
        <span class="nv-perm__text">
          <span :id="`${id}-${flag}`" class="nv-perm__label">{{ t(`nova.devices.perm_${flag}`) }}</span>
          <span :id="`${id}-${flag}-desc`" class="nv-perm__desc">
            {{ t(`nova.devices.perm_${flag}_desc`) }}
            <template v-if="flag === 'host_commands' && commandsHint"><br>{{ commandsHint }}</template>
          </span>
        </span>
        <NvSwitch :model-value="flags[flag]" :labelledby="`${id}-${flag}`" :describedby="`${id}-${flag}-desc`"
                  :disabled="disabled" @update:model-value="(value) => setFlag(flag, value)" />
      </li>
    </ul>
  </div>
</template>

<style scoped>
@layer components {
  .nv-perm {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
  }

  .nv-perm__presets {
    width: 100%;
  }

  .nv-perm__presets :deep(.nv-seg__option) {
    flex: 1 1 0;
  }

  .nv-perm__preset-desc {
    margin: 0 0 var(--nv-space-1);
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
  }

  .nv-perm__group {
    margin: var(--nv-space-2) 0 0;
    font-size: var(--nv-text-xs);
    font-weight: 600;
    letter-spacing: 0.02em;
    text-transform: uppercase;
    color: var(--nv-text-secondary);
  }

  .nv-perm__flags {
    margin: 0;
    padding: 0;
    list-style: none;
    border: 1px solid var(--nv-border);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-surface);
  }

  .nv-perm__flag {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 44px;
    padding: var(--nv-space-2) var(--nv-space-3) var(--nv-space-2) var(--nv-space-4);
    border-bottom: 1px solid var(--nv-divider);
  }

  .nv-perm__flag:last-child {
    border-bottom: 0;
  }

  .nv-perm__text {
    display: flex;
    flex-direction: column;
    flex-grow: 1;
    min-width: 0;
  }

  .nv-perm__label {
    font-size: var(--nv-text-md);
    color: var(--nv-text);
  }

  .nv-perm__desc {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
  }
}
</style>

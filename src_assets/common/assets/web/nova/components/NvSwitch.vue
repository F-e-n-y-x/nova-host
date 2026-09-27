<script setup>
/**
 * On/off switch (role="switch" on a native button, so Space and Enter toggle it).
 *
 * v-model: boolean.
 * Props: label (accessible name; required unless labelledby is set), labelledby (id of a
 *        visible label, e.g. NvSettingRow's labelId), describedby, showState (prints On/Off
 *        next to the switch), disabled, id.
 */
import { useI18n } from 'vue-i18n'

const model = defineModel({ type: Boolean, default: false })
defineProps({
  label: { type: String, default: '' },
  labelledby: { type: String, default: null },
  describedby: { type: String, default: null },
  showState: { type: Boolean, default: false },
  disabled: { type: Boolean, default: false },
  id: { type: String, default: null },
})

const { t } = useI18n()
</script>

<template>
  <span class="nv-switch-wrap">
    <span v-if="showState" class="nv-switch__state" aria-hidden="true">{{ model ? t('nova.common.on') : t('nova.common.off') }}</span>
    <button :id="id" type="button" role="switch" :aria-checked="model ? 'true' : 'false'"
            :aria-label="labelledby ? null : label" :aria-labelledby="labelledby" :aria-describedby="describedby"
            :disabled="disabled" :class="['nv-switch', { 'nv-switch--on': model }]" @click="model = !model">
      <span class="nv-switch__knob" aria-hidden="true"></span>
    </button>
  </span>
</template>

<style>
@layer components {
  .nv-switch-wrap {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-2);
  }

  .nv-switch__state {
    min-width: 24px;
    text-align: right;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-switch {
    position: relative;
    width: 40px;
    height: 24px;
    flex-shrink: 0;
    padding: 0;
    border-radius: 12px;
    border: 1px solid var(--nv-border-strong);
    background: var(--nv-raised);
    cursor: pointer;
    transition: background-color 150ms ease-out, border-color 150ms ease-out;
  }

  .nv-switch__knob {
    position: absolute;
    top: 3px;
    left: 3px;
    width: 16px;
    height: 16px;
    border-radius: 8px;
    background: var(--nv-text-secondary);
    transition: transform 150ms ease-out, background-color 150ms ease-out;
  }

  .nv-switch--on {
    background: var(--nv-accent);
    border-color: var(--nv-accent);
  }

  .nv-switch--on .nv-switch__knob {
    transform: translateX(16px);
    background: #FFFFFF;
  }

  .nv-switch:disabled {
    cursor: not-allowed;
    opacity: 0.5;
  }

  @media (max-width: 767px) {
    .nv-switch::after {
      content: "";
      position: absolute;
      inset: -10px -4px;
    }
  }
}
</style>

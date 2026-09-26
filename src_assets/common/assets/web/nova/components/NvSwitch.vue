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
    flex-shrink: 0;
    width: 44px;
    height: 26px;
    padding: 0;
    border-radius: 13px;
    border: 1px solid var(--nv-border-strong);
    background: var(--nv-raised);
    cursor: pointer;
    transition: background-color 120ms ease, border-color 120ms ease;
  }

  .nv-switch__knob {
    position: absolute;
    top: 3px;
    left: 3px;
    width: 18px;
    height: 18px;
    border-radius: 9px;
    background: var(--nv-text-secondary);
    transition: left 120ms ease;
  }

  .nv-switch--on {
    background: var(--nv-accent);
    border-color: var(--nv-accent);
  }

  .nv-switch--on .nv-switch__knob {
    left: 21px;
    background: #FFFFFF;
  }

  .nv-switch:disabled {
    opacity: 0.55;
    cursor: not-allowed;
  }
}
</style>

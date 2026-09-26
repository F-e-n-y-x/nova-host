<script setup>
/**
 * One setting: label + description on the left, the control on the right.
 * Shows a "Changed" marker and a Reset button when `modified` is true, and an
 * optional warning under the description.
 *
 * Props: label (required), description, modified, warning, controlId (id of the control,
 *        so the label becomes a <label for>), resetLabel.
 * Emits: reset.
 * Slot: default — the control; receives { labelId, descriptionId } to wire aria-labelledby /
 *       aria-describedby on controls that are not native form fields (NvSwitch, NvSegmentedControl).
 */
import { useId } from 'vue'
import { useI18n } from 'vue-i18n'
import { TriangleAlert } from '@lucide/vue'

defineProps({
  label: { type: String, required: true },
  description: { type: String, default: '' },
  modified: { type: Boolean, default: false },
  warning: { type: String, default: '' },
  controlId: { type: String, default: null },
  resetLabel: { type: String, default: '' },
})
defineEmits(['reset'])

const { t } = useI18n()
const baseId = useId()
const labelId = `${baseId}-label`
const descriptionId = `${baseId}-desc`
</script>

<template>
  <div class="nv-setting">
    <div class="nv-setting__text">
      <div class="nv-setting__label-row">
        <label v-if="controlId" :id="labelId" :for="controlId" class="nv-setting__label">{{ label }}</label>
        <span v-else :id="labelId" class="nv-setting__label">{{ label }}</span>
        <span v-if="modified" class="nv-setting__changed"><span class="nv-setting__changed-dot" aria-hidden="true"></span>{{ t('nova.common.changed') }}</span>
      </div>
      <p v-if="description" :id="descriptionId" class="nv-setting__desc">{{ description }}</p>
      <div v-if="warning" class="nv-setting__warning" role="note">
        <TriangleAlert :size="18" aria-hidden="true" class="nv-setting__warning-icon" />
        <span>{{ warning }}</span>
      </div>
    </div>
    <div class="nv-setting__control">
      <button v-if="modified" type="button" class="nv-setting__reset" @click="$emit('reset')">
        {{ t('nova.common.reset') }}<span class="nv-visually-hidden"> {{ resetLabel || label }}</span>
      </button>
      <slot :label-id="labelId" :description-id="description ? descriptionId : null" />
    </div>
  </div>
</template>

<style>
@layer components {
  .nv-setting {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-6);
    padding: 18px var(--nv-space-5);
    border-bottom: 1px solid var(--nv-border);
  }

  .nv-setting:last-child {
    border-bottom: 0;
  }

  .nv-setting__text {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
    flex-grow: 1;
    min-width: 0;
  }

  .nv-setting__label-row {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: var(--nv-space-2);
  }

  .nv-setting__label {
    font-weight: 500;
    margin: 0;
  }

  .nv-setting__changed {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    font-size: var(--nv-text-xs);
    font-weight: 500;
    color: var(--nv-accent-text);
  }

  .nv-setting__changed-dot {
    width: 6px;
    height: 6px;
    border-radius: 3px;
    background: var(--nv-accent-text);
  }

  .nv-setting__desc {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-setting__warning {
    display: flex;
    gap: 10px;
    margin-top: 6px;
    padding: 10px var(--nv-space-3);
    border-radius: var(--nv-radius-md);
    background: var(--nv-warning-tint);
    font-size: var(--nv-text-sm);
    color: var(--nv-text);
  }

  .nv-setting__warning-icon {
    flex-shrink: 0;
    margin-top: 1px;
    color: var(--nv-warning);
  }

  .nv-setting__control {
    display: flex;
    align-items: center;
    gap: 10px;
    flex-shrink: 0;
  }

  .nv-setting__reset {
    min-height: var(--nv-control-height-sm);
    padding: 0 var(--nv-space-2);
    border: 0;
    border-radius: var(--nv-radius-md);
    background: transparent;
    color: var(--nv-accent-text);
    font: inherit;
    font-size: var(--nv-text-sm);
    font-weight: 500;
    cursor: pointer;
  }

  .nv-setting__reset:hover {
    background: var(--nv-accent-tint);
  }

  @media (max-width: 699px) {
    .nv-setting {
      flex-direction: column;
      gap: var(--nv-space-3);
    }

    .nv-setting__control {
      flex-wrap: wrap;
    }
  }
}
</style>

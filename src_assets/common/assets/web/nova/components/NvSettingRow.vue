<script setup>
/**
 * One setting row (SPEC §5 Settings): label + description left, control right from 720px
 * (control column ≥ 240px), stacked below. Rows sit inside a flush NvCard and are separated by
 * the divider — never wrap them in another bordered box.
 *
 * Props: label (required), description (≤ 2 lines), more (long help, behind a "More" toggle; the
 *        `more` slot works too), modified (shows "● Changed" + Reset), warning, error (inline,
 *        replaces nothing), controlId (makes the label a <label for>), resetLabel, full (control
 *        spans the full width under the text — list editors, tables).
 * Emits: reset.
 * Slot: default — the control; receives { labelId, descriptionId } for aria-labelledby /
 *       aria-describedby on non-native controls (NvSwitch, NvSegmentedControl).
 */
import { computed, ref, useId, useSlots } from 'vue'
import { useI18n } from 'vue-i18n'
import { ChevronDown, CircleAlert, TriangleAlert } from '@lucide/vue'

const props = defineProps({
  label: { type: String, required: true },
  description: { type: String, default: '' },
  more: { type: String, default: '' },
  modified: { type: Boolean, default: false },
  warning: { type: String, default: '' },
  error: { type: String, default: '' },
  controlId: { type: String, default: null },
  resetLabel: { type: String, default: '' },
  full: { type: Boolean, default: false },
})
defineEmits(['reset'])

const { t } = useI18n()
const slots = useSlots()
const baseId = useId()
const labelId = `${baseId}-label`
const descriptionId = `${baseId}-desc`
const moreId = `${baseId}-more`
const showMore = ref(false)
const hasMore = computed(() => Boolean(props.more || slots.more))
const describedBy = computed(() => (props.description || props.error ? descriptionId : null))
</script>

<template>
  <div :class="['nv-setting', { 'nv-setting--full': full, 'nv-setting--invalid': error }]">
    <div class="nv-setting__grid">
    <div class="nv-setting__text">
      <div class="nv-setting__label-row">
        <label v-if="controlId" :id="labelId" :for="controlId" class="nv-setting__label">{{ label }}</label>
        <span v-else :id="labelId" class="nv-setting__label">{{ label }}</span>
        <span v-if="modified" class="nv-setting__changed"><span class="nv-setting__changed-dot" aria-hidden="true"></span>{{ t('nova.common.changed') }}</span>
        <button v-if="modified" type="button" class="nv-setting__reset" @click="$emit('reset')">
          {{ t('nova.common.reset') }}<span class="nv-visually-hidden"> {{ resetLabel || label }}</span>
        </button>
      </div>
      <p v-if="description || error" :id="descriptionId" class="nv-setting__desc">
        <span v-if="description" class="nv-clamp-2">{{ description }}</span>
        <span v-if="error" class="nv-setting__error"><CircleAlert :size="14" aria-hidden="true" />{{ error }}</span>
      </p>
      <button v-if="hasMore" type="button" class="nv-setting__more-toggle" :aria-expanded="showMore ? 'true' : 'false'"
              :aria-controls="moreId" @click="showMore = !showMore">
        {{ showMore ? t('nova.common.less') : t('nova.common.more') }}
        <ChevronDown :size="14" aria-hidden="true" :class="{ 'nv-setting__chev--open': showMore }" />
      </button>
      <div v-if="hasMore" v-show="showMore" :id="moreId" class="nv-setting__more">
        <slot name="more">{{ more }}</slot>
      </div>
      <div v-if="warning" class="nv-setting__warning" role="note">
        <TriangleAlert :size="16" aria-hidden="true" class="nv-setting__warning-icon" />
        <span>{{ warning }}</span>
      </div>
    </div>
    <div class="nv-setting__control">
      <slot :label-id="labelId" :description-id="describedBy" />
    </div>
    </div>
  </div>
</template>

<style>
@layer components {
  /* The row is a size container so its layout follows the space it actually gets (a settings
     card, a 440px side panel, a phone), not the viewport. */
  .nv-setting {
    container-type: inline-size;
    padding: var(--nv-space-4) var(--nv-space-5);
    border-bottom: 1px solid var(--nv-divider);
  }

  .nv-setting__grid {
    display: grid;
    grid-template-columns: minmax(0, 1fr);
    gap: var(--nv-space-3);
  }

  .nv-setting:last-child {
    border-bottom: 0;
  }

  .nv-setting__text {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
    min-width: 0;
  }

  .nv-setting__label-row {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: var(--nv-space-2) var(--nv-space-3);
  }

  .nv-setting__label {
    font-size: var(--nv-text-md);
    font-weight: 500;
    color: var(--nv-text);
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

  .nv-setting__reset,
  .nv-setting__more-toggle {
    display: inline-flex;
    align-items: center;
    gap: 2px;
    min-height: 24px;
    padding: 0;
    border: 0;
    background: transparent;
    color: var(--nv-accent-text);
    font: inherit;
    font-size: var(--nv-text-xs);
    font-weight: 500;
    cursor: pointer;
  }

  .nv-setting__more-toggle {
    align-self: flex-start;
    color: var(--nv-text-secondary);
  }

  .nv-setting__reset:hover,
  .nv-setting__more-toggle:hover {
    text-decoration: underline;
  }

  .nv-setting__chev--open {
    transform: rotate(180deg);
  }

  .nv-setting__desc {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
    font-size: var(--nv-text-sm);
    line-height: 18px;
    color: var(--nv-text-secondary);
  }

  .nv-setting__error {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-1);
    color: var(--nv-danger);
  }

  .nv-setting__more {
    font-size: var(--nv-text-sm);
    line-height: 18px;
    color: var(--nv-text-secondary);
    white-space: pre-line;
  }

  .nv-setting__warning {
    display: flex;
    gap: var(--nv-space-2);
    margin-top: var(--nv-space-1);
    font-size: var(--nv-text-sm);
    line-height: 18px;
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
    gap: var(--nv-space-3);
    min-width: 0;
  }

  @container (min-width: 360px) {
    .nv-setting:not(.nv-setting--full) > .nv-setting__grid {
      grid-template-columns: minmax(0, 1fr) auto;
      align-items: start;
      gap: var(--nv-space-4);
    }

    .nv-setting:not(.nv-setting--full) .nv-setting__control {
      justify-content: flex-end;
    }
  }

  @container (min-width: 560px) {
    .nv-setting:not(.nv-setting--full) > .nv-setting__grid {
      grid-template-columns: minmax(0, 1fr) minmax(240px, auto);
      gap: var(--nv-space-6);
    }
  }

  @media (max-width: 767px) {
    .nv-setting {
      padding: var(--nv-space-4);
    }
  }
}
</style>

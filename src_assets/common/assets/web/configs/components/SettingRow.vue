<script setup>
/**
 * One setting row, laid out like NvSettingRow (label and description left, control right,
 * "Changed" marker and Reset), plus: an optional "More" disclosure for longer help, an
 * inline error, and a full-width `details` slot for list editors.
 *
 * Page-local until NvSettingRow gains a details slot.
 *
 * Props: id (anchor for #links), label, description, more (extra paragraphs behind "More"),
 *        moreOpen, modified, warning, error, resetLabel.
 * Emits: reset.
 * Slots: default (control; gets { labelId, descriptionId }), more (extra help content),
 *        details (full-width content under the text).
 */
import { shallowRef, useId } from 'vue'
import { useI18n } from 'vue-i18n'
import { ChevronDown, CircleAlert, TriangleAlert } from '@lucide/vue'

const props = defineProps({
  id: { type: String, default: null },
  label: { type: String, required: true },
  description: { type: String, default: '' },
  more: { type: Array, default: () => [] },
  hasMoreSlot: { type: Boolean, default: false },
  moreOpen: { type: Boolean, default: false },
  modified: { type: Boolean, default: false },
  warning: { type: String, default: '' },
  error: { type: String, default: '' },
  resetLabel: { type: String, default: '' },
})
defineEmits(['reset'])

const { t } = useI18n()
const baseId = useId()
const labelId = `${baseId}-label`
const descriptionId = `${baseId}-desc`
const moreId = `${baseId}-more`
const expanded = shallowRef(props.moreOpen)
</script>

<template>
  <div :id="id" class="nv-cfg-row" tabindex="-1">
    <div class="nv-cfg-row__main">
      <div class="nv-cfg-row__text">
        <div class="nv-cfg-row__label-row">
          <span :id="labelId" class="nv-cfg-row__label">{{ label }}</span>
          <span v-if="modified" class="nv-cfg-row__changed">
            <span class="nv-cfg-row__changed-dot" aria-hidden="true"></span>{{ t('nova.common.changed') }}
          </span>
        </div>
        <p v-if="description" :id="descriptionId" class="nv-cfg-row__desc">{{ description }}</p>
        <button v-if="more.length || hasMoreSlot" type="button" class="nv-cfg-row__more-toggle"
                :aria-expanded="expanded ? 'true' : 'false'" :aria-controls="moreId" @click="expanded = !expanded">
          {{ expanded ? t('nova.settings.less') : t('nova.settings.more') }}
          <ChevronDown :size="14" aria-hidden="true" :class="{ 'nv-cfg-row__chevron--open': expanded }" />
        </button>
        <div v-show="expanded" :id="moreId" class="nv-cfg-row__more">
          <p v-for="(paragraph, i) in more" :key="i">{{ paragraph }}</p>
          <slot name="more" />
        </div>
        <div v-if="warning" class="nv-cfg-row__note nv-cfg-row__note--warning" role="note">
          <TriangleAlert :size="18" aria-hidden="true" class="nv-cfg-row__note-icon" />
          <span>{{ warning }}</span>
        </div>
        <p v-if="error" class="nv-cfg-row__error" role="alert">
          <CircleAlert :size="16" aria-hidden="true" />{{ error }}
        </p>
      </div>
      <div class="nv-cfg-row__control">
        <button v-if="modified" type="button" class="nv-cfg-row__reset" @click="$emit('reset')">
          {{ t('nova.common.reset') }}<span class="nv-visually-hidden"> {{ resetLabel || label }}</span>
        </button>
        <slot :label-id="labelId" :description-id="description ? descriptionId : null" />
      </div>
    </div>
    <slot name="details" />
  </div>
</template>

<style>
@layer components {
  .nv-cfg-row {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
    padding: 18px var(--nv-space-5);
    border-bottom: 1px solid var(--nv-border);
    scroll-margin-top: 96px;
  }

  .nv-cfg-row:last-child {
    border-bottom: 0;
  }

  .nv-cfg-row:focus {
    outline: none;
  }

  .nv-cfg-row:target,
  .nv-cfg-row--flash {
    box-shadow: inset 3px 0 0 var(--nv-accent);
  }

  .nv-cfg-row__main {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-6);
  }

  .nv-cfg-row__text {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
    flex-grow: 1;
    min-width: 0;
  }

  .nv-cfg-row__label-row {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: var(--nv-space-2);
  }

  .nv-cfg-row__label {
    font-weight: 500;
  }

  .nv-cfg-row__changed {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    font-size: var(--nv-text-xs);
    font-weight: 500;
    color: var(--nv-accent-text);
  }

  .nv-cfg-row__changed-dot {
    width: 6px;
    height: 6px;
    border-radius: 3px;
    background: var(--nv-accent-text);
  }

  .nv-cfg-row__desc,
  .nv-cfg-row__more p {
    margin: 0;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-cfg-row__more {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    margin-top: var(--nv-space-1);
  }

  .nv-cfg-row__more-toggle {
    align-self: flex-start;
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-1);
    min-height: 28px;
    padding: 0;
    border: 0;
    background: transparent;
    color: var(--nv-accent-text);
    font: inherit;
    font-size: var(--nv-text-sm);
    font-weight: 500;
    cursor: pointer;
  }

  .nv-cfg-row__more-toggle:hover {
    text-decoration: underline;
  }

  .nv-cfg-row__chevron--open {
    transform: rotate(180deg);
  }

  .nv-cfg-row__note {
    display: flex;
    gap: 10px;
    margin-top: 6px;
    padding: 10px var(--nv-space-3);
    border-radius: var(--nv-radius-md);
    font-size: var(--nv-text-sm);
    color: var(--nv-text);
  }

  .nv-cfg-row__note--warning {
    background: var(--nv-warning-tint);
  }

  .nv-cfg-row__note-icon {
    flex-shrink: 0;
    margin-top: 1px;
    color: var(--nv-warning);
  }

  .nv-cfg-row__error {
    display: flex;
    align-items: center;
    gap: 6px;
    margin: 4px 0 0;
    font-size: var(--nv-text-sm);
    font-weight: 500;
    color: var(--nv-danger);
  }

  .nv-cfg-row__control {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    justify-content: flex-end;
    gap: 10px;
    flex-shrink: 0;
    max-width: 55%;
  }

  .nv-cfg-row__reset {
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

  .nv-cfg-row__reset:hover {
    background: var(--nv-accent-tint);
  }

  @media (max-width: 699px) {
    .nv-cfg-row {
      padding: var(--nv-space-4);
    }

    .nv-cfg-row__main {
      flex-direction: column;
      gap: var(--nv-space-3);
    }

    .nv-cfg-row__control {
      align-self: stretch;
      justify-content: flex-start;
      max-width: none;
    }
  }
}
</style>

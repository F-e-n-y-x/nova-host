<script setup>
/**
 * Inline notice. Text sits on a tint of its status colour (all pairs meet WCAG AA).
 *
 * Props: variant ('info' | 'success' | 'warning' | 'danger'), title, live (announce to screen
 *        readers when it appears: role=alert for danger/warning, role=status otherwise),
 *        dismissible (shows a close button; emits dismiss).
 * Slots: default (body), actions.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { CircleAlert, CircleCheck, Info, TriangleAlert, X } from '@lucide/vue'

const props = defineProps({
  variant: { type: String, default: 'info' },
  title: { type: String, default: '' },
  live: { type: Boolean, default: false },
  dismissible: { type: Boolean, default: false },
})
defineEmits(['dismiss'])

const { t } = useI18n()
const icon = computed(() => ({ success: CircleCheck, warning: TriangleAlert, danger: CircleAlert }[props.variant] ?? Info))
const role = computed(() => {
  if (!props.live) return null
  return props.variant === 'danger' || props.variant === 'warning' ? 'alert' : 'status'
})
</script>

<template>
  <div :class="['nv-alert', `nv-alert--${variant}`]" :role="role">
    <component :is="icon" :size="20" class="nv-alert__icon" aria-hidden="true" />
    <div class="nv-alert__body">
      <p v-if="title" class="nv-alert__title">{{ title }}</p>
      <div v-if="$slots.default" class="nv-alert__text"><slot /></div>
      <div v-if="$slots.actions" class="nv-alert__actions"><slot name="actions" /></div>
    </div>
    <button v-if="dismissible" type="button" class="nv-alert__close" :aria-label="t('nova.common.dismiss')" @click="$emit('dismiss')">
      <X :size="16" aria-hidden="true" />
    </button>
  </div>
</template>

<style>
@layer components {
  .nv-alert {
    --nv-alert-fg: var(--nv-accent-text);
    --nv-alert-bg: var(--nv-accent-tint);
    display: flex;
    gap: var(--nv-space-3);
    padding: var(--nv-space-3) var(--nv-space-4);
    border-radius: var(--nv-radius-md);
    background: var(--nv-alert-bg);
    color: var(--nv-text);
  }

  .nv-alert--success {
    --nv-alert-fg: var(--nv-success);
    --nv-alert-bg: var(--nv-success-tint);
  }

  .nv-alert--warning {
    --nv-alert-fg: var(--nv-warning);
    --nv-alert-bg: var(--nv-warning-tint);
  }

  .nv-alert--danger {
    --nv-alert-fg: var(--nv-danger);
    --nv-alert-bg: var(--nv-danger-tint);
  }

  .nv-alert__icon {
    flex-shrink: 0;
    margin-top: 1px;
    color: var(--nv-alert-fg);
  }

  .nv-alert__body {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
    flex-grow: 1;
    min-width: 0;
  }

  .nv-alert__title {
    font-weight: 500;
    color: var(--nv-alert-fg);
  }

  .nv-alert__text {
    font-size: var(--nv-text-sm);
  }

  .nv-alert__actions {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-2);
    margin-top: var(--nv-space-1);
  }

  .nv-alert__actions a {
    color: var(--nv-alert-fg);
    font-size: var(--nv-text-sm);
    font-weight: 500;
  }

  .nv-alert__close {
    flex-shrink: 0;
    width: 32px;
    height: 32px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border: 0;
    border-radius: var(--nv-radius-sm);
    background: transparent;
    color: var(--nv-text-secondary);
    cursor: pointer;
  }
}
</style>

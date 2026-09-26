<script setup>
/**
 * Icon-only button. `label` is required: it becomes the aria-label and tooltip.
 *
 * Props: label (required), variant ('ghost' | 'secondary'), size ('md' | 'sm'), disabled, type.
 * Slot: default — the icon (e.g. a Lucide component).
 */
defineProps({
  label: { type: String, required: true },
  variant: { type: String, default: 'ghost' },
  size: { type: String, default: 'md' },
  disabled: { type: Boolean, default: false },
  type: { type: String, default: 'button' },
})
</script>

<template>
  <button :type="type" :class="['nv-icon-btn', `nv-icon-btn--${variant}`, `nv-icon-btn--${size}`]"
          :aria-label="label" :title="label" :disabled="disabled">
    <slot />
  </button>
</template>

<style>
@layer components {
  .nv-icon-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: var(--nv-control-height);
    height: var(--nv-control-height);
    padding: 0;
    border-radius: var(--nv-radius-md);
    border: 1px solid transparent;
    background: transparent;
    color: var(--nv-text-secondary);
    cursor: pointer;
  }

  .nv-icon-btn--sm {
    width: var(--nv-control-height-sm);
    height: var(--nv-control-height-sm);
  }

  .nv-icon-btn:hover:not(:disabled) {
    background: var(--nv-raised);
    color: var(--nv-text);
  }

  .nv-icon-btn--secondary {
    border-color: var(--nv-border-strong);
  }

  .nv-icon-btn:disabled {
    opacity: 0.55;
    cursor: not-allowed;
  }
}
</style>

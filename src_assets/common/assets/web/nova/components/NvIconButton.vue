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
    width: 32px;
    height: 32px;
    flex-shrink: 0;
    padding: 0;
    border-radius: var(--nv-radius-md);
    border: 1px solid transparent;
    background: transparent;
    color: var(--nv-text-secondary);
    cursor: pointer;
    transition: background-color 150ms ease-out, color 150ms ease-out;
  }

  .nv-icon-btn:hover:not(:disabled) {
    background: var(--nv-raised);
    color: var(--nv-text);
  }

  .nv-icon-btn--secondary {
    border-color: var(--nv-border-strong);
  }

  .nv-icon-btn--sm {
    width: 28px;
    height: 28px;
  }

  .nv-icon-btn:disabled {
    cursor: not-allowed;
    opacity: 0.5;
  }

  @media (max-width: 767px) {
    .nv-icon-btn {
      width: 44px;
      height: 44px;
    }
  }
}
</style>

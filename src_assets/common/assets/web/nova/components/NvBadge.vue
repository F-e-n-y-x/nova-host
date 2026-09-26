<script setup>
/**
 * Small status label.
 *
 * Props: variant ('neutral' | 'accent' | 'success' | 'warning' | 'danger').
 * Slot: default — the text.
 */
defineProps({
  variant: { type: String, default: 'neutral' },
})
</script>

<template>
  <span :class="['nv-badge', `nv-badge--${variant}`]"><slot /></span>
</template>

<style>
@layer components {
  .nv-badge {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-1);
    padding: 2px var(--nv-space-2);
    border-radius: 10px;
    font-size: var(--nv-text-xs);
    font-weight: 500;
    line-height: 1.5;
    white-space: nowrap;
    background: var(--nv-raised);
    color: var(--nv-text-secondary);
  }

  .nv-badge--accent {
    background: var(--nv-accent-tint);
    color: var(--nv-accent-text);
  }

  .nv-badge--success {
    background: var(--nv-success-tint);
    color: var(--nv-success);
  }

  .nv-badge--warning {
    background: var(--nv-warning-tint);
    color: var(--nv-warning);
  }

  .nv-badge--danger {
    background: var(--nv-danger-tint);
    color: var(--nv-danger);
  }
}
</style>

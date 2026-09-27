<script setup>
/**
 * Coloured status dot with a text label (colour is never the only signal).
 *
 * Props: status ('success' | 'warning' | 'danger' | 'neutral'), label (required),
 *        hideLabel (label read by screen readers only), tone ('text' keeps the label in the
 *        normal text colour; 'status' colours it like the dot).
 */
defineProps({
  status: { type: String, default: 'neutral' },
  label: { type: String, required: true },
  hideLabel: { type: Boolean, default: false },
  tone: { type: String, default: 'text' },
})
</script>

<template>
  <span :class="['nv-status', `nv-status--${status}`, `nv-status--tone-${tone}`]">
    <span class="nv-status__dot" aria-hidden="true"></span>
    <span :class="{ 'nv-visually-hidden': hideLabel }">{{ label }}</span>
  </span>
</template>

<style>
@layer components {
  .nv-status {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-2);
    color: var(--nv-text);
  }

  .nv-status__dot {
    width: 6px;
    height: 6px;
    flex-shrink: 0;
    border-radius: 3px;
    background: var(--nv-text-muted);
  }

  .nv-status--success .nv-status__dot {
    background: var(--nv-success);
    box-shadow: 0 0 0 4px var(--nv-success-tint);
  }

  .nv-status--warning .nv-status__dot {
    background: var(--nv-warning);
  }

  .nv-status--danger .nv-status__dot {
    background: var(--nv-danger);
  }

  .nv-status--accent .nv-status__dot {
    background: var(--nv-accent-text);
  }

  .nv-status--tone-status.nv-status--success {
    color: var(--nv-success);
  }

  .nv-status--tone-status.nv-status--warning {
    color: var(--nv-warning);
  }

  .nv-status--tone-status.nv-status--danger {
    color: var(--nv-danger);
  }

  .nv-status--tone-muted {
    color: var(--nv-text-secondary);
  }
}
</style>

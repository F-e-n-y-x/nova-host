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
    --nv-status-color: var(--nv-text-muted);
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-2);
  }

  .nv-status--success {
    --nv-status-color: var(--nv-success);
  }

  .nv-status--warning {
    --nv-status-color: var(--nv-warning);
  }

  .nv-status--danger {
    --nv-status-color: var(--nv-danger);
  }

  .nv-status__dot {
    width: 8px;
    height: 8px;
    flex-shrink: 0;
    border-radius: 4px;
    background: var(--nv-status-color);
  }

  .nv-status--tone-status {
    color: var(--nv-status-color);
    font-weight: 500;
  }
}
</style>

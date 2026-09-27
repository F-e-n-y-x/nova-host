<script setup>
/**
 * Empty or zero-data state.
 *
 * Props: title (required), description, compact (less padding, for use inside cards).
 * Slots: icon, actions.
 */
defineProps({
  title: { type: String, required: true },
  description: { type: String, default: '' },
  compact: { type: Boolean, default: false },
})
</script>

<template>
  <div :class="['nv-empty', { 'nv-empty--compact': compact }]">
    <div v-if="$slots.icon" class="nv-empty__icon" aria-hidden="true"><slot name="icon" /></div>
    <p class="nv-empty__title">{{ title }}</p>
    <p v-if="description" class="nv-empty__desc">{{ description }}</p>
    <div v-if="$slots.actions" class="nv-empty__actions"><slot name="actions" /></div>
  </div>
</template>

<style>
@layer components {
  .nv-empty {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: var(--nv-space-2);
    padding: var(--nv-space-10) var(--nv-space-6);
    text-align: center;
  }

  .nv-empty--compact {
    padding: var(--nv-space-6) var(--nv-space-4);
  }

  .nv-empty__icon {
    display: flex;
    align-items: center;
    justify-content: center;
    width: 40px;
    height: 40px;
    margin-bottom: var(--nv-space-1);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-raised);
    color: var(--nv-text-secondary);
  }

  .nv-empty__title {
    font-size: var(--nv-text-md);
    font-weight: 600;
    color: var(--nv-text);
  }

  .nv-empty__desc {
    max-width: 44ch;
    font-size: var(--nv-text-sm);
    line-height: 18px;
    color: var(--nv-text-secondary);
  }

  .nv-empty__actions {
    display: flex;
    flex-wrap: wrap;
    justify-content: center;
    gap: var(--nv-space-2);
    margin-top: var(--nv-space-2);
  }
}
</style>

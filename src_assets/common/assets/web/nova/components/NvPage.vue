<script setup>
/**
 * Standard page frame inside the app shell: title row with optional subtitle and
 * actions, then the page body at the shared content width and padding.
 *
 * Props: title (required; the page's only <h1>), wide (no max width, for dense tables).
 * Slots: subtitle, actions, default.
 */
import { watchEffect } from 'vue'

const props = defineProps({
  title: { type: String, required: true },
  wide: { type: Boolean, default: false },
})

watchEffect(() => {
  if (typeof document !== 'undefined') document.title = `${props.title} · Nova`
})
</script>

<template>
  <div :class="['nv-page', { 'nv-page--wide': wide }]">
    <header class="nv-page__header">
      <div class="nv-page__heading">
        <h1>{{ title }}</h1>
        <div v-if="$slots.subtitle" class="nv-page__subtitle"><slot name="subtitle" /></div>
      </div>
      <div v-if="$slots.actions" class="nv-page__actions"><slot name="actions" /></div>
    </header>
    <slot />
  </div>
</template>

<style>
@layer components {
  .nv-page {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-6);
    max-width: var(--nv-content-max);
    padding: 28px var(--nv-space-10) var(--nv-space-10);
  }

  .nv-page--wide {
    max-width: none;
  }

  .nv-page__header {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: var(--nv-space-4);
  }

  .nv-page__heading {
    display: flex;
    flex-direction: column;
    gap: 2px;
    flex-grow: 1;
    min-width: 0;
  }

  .nv-page__subtitle {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: var(--nv-space-2);
    color: var(--nv-text-secondary);
  }

  .nv-page__actions {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-3);
  }

  @media (max-width: 899px) {
    .nv-page {
      padding: var(--nv-space-5) var(--nv-space-4) var(--nv-space-8);
      gap: var(--nv-space-5);
    }
  }
}
</style>

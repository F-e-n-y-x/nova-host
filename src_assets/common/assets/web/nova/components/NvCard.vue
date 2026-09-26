<script setup>
/**
 * Card / page section with an optional heading. The heading labels the region.
 *
 * Props: title, level (heading level, default 2), as (root element, default 'section'),
 *        span (grid columns to span, 2 or 3, from 900px wide up), flush (no padding).
 * Slots: default, actions (right side of the header), footer.
 */
import { useId } from 'vue'

const props = defineProps({
  title: { type: String, default: '' },
  level: { type: Number, default: 2 },
  as: { type: String, default: 'section' },
  span: { type: Number, default: 1 },
  flush: { type: Boolean, default: false },
})

const headingId = useId()
</script>

<template>
  <component :is="as" :class="['nv-card', { 'nv-card--flush': flush, [`nv-card--span-${span}`]: span > 1 }]"
             :aria-labelledby="title ? headingId : null">
    <header v-if="title || $slots.actions" class="nv-card__header">
      <component :is="`h${level}`" v-if="title" :id="headingId" class="nv-card__title">{{ title }}</component>
      <div v-if="$slots.actions" class="nv-card__actions"><slot name="actions" /></div>
    </header>
    <slot />
    <footer v-if="$slots.footer" class="nv-card__footer"><slot name="footer" /></footer>
  </component>
</template>

<style>
@layer components {
  .nv-card {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
    min-width: 0;
    padding: var(--nv-space-6);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-surface);
    border: 1px solid var(--nv-border);
    color: var(--nv-text);
  }

  .nv-card--flush {
    padding: 0;
    gap: 0;
  }

  .nv-card__header {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
  }

  .nv-card--flush .nv-card__header {
    padding: var(--nv-space-5) var(--nv-space-6) var(--nv-space-3);
  }

  .nv-card__title {
    flex-grow: 1;
    font-size: var(--nv-text-lg);
  }

  .nv-card__actions {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
  }

  .nv-card__footer {
    margin-top: auto;
    display: flex;
    gap: var(--nv-space-3);
    flex-wrap: wrap;
  }

  @media (min-width: 900px) {
    .nv-card--span-2 {
      grid-column: span 2;
    }

    .nv-card--span-3 {
      grid-column: span 3;
    }
  }

  @media (max-width: 899px) {
    .nv-card {
      padding: var(--nv-space-5);
    }
  }
}
</style>

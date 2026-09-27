<script setup>
/**
 * Card (SPEC §5): surface, 1px border, radius 12, 52px header row (title left, one text link or
 * small control right) separated from the body by a divider. One level of containment: put rows
 * inside with NvList/NvTable, never another bordered box.
 *
 * Props: title, level (heading level, default 2), as (root element, default 'section'),
 *        cols (columns to span in the 12-column page grid from 1024px), span (legacy: 2 or 3 of a
 *        3-column grid), flush (no body padding — tables, lists), plain (no header divider).
 * Slots: default, actions (header right), footer (bottom row, divider above).
 */
import { useId } from 'vue'

defineProps({
  title: { type: String, default: '' },
  level: { type: Number, default: 2 },
  as: { type: String, default: 'section' },
  cols: { type: Number, default: 0 },
  span: { type: Number, default: 1 },
  flush: { type: Boolean, default: false },
  plain: { type: Boolean, default: false },
})

const headingId = useId()
</script>

<template>
  <component :is="as"
             :class="['nv-card', { 'nv-card--flush': flush, 'nv-card--plain': plain, [`nv-card--span-${span}`]: span > 1 }]"
             :style="cols ? { '--nv-card-cols': cols } : null" :data-cols="cols || null"
             :aria-labelledby="title ? headingId : null">
    <header v-if="title || $slots.actions" class="nv-card__header">
      <component :is="`h${level}`" v-if="title" :id="headingId" class="nv-card__title">{{ title }}</component>
      <div v-if="$slots.actions" class="nv-card__actions"><slot name="actions" /></div>
    </header>
    <div class="nv-card__body"><slot /></div>
    <footer v-if="$slots.footer" class="nv-card__footer"><slot name="footer" /></footer>
  </component>
</template>

<style>
@layer components {
  .nv-card {
    display: flex;
    flex-direction: column;
    min-width: 0;
    border-radius: var(--nv-radius-xl);
    background: var(--nv-surface);
    border: 1px solid var(--nv-border);
    color: var(--nv-text);
    overflow: hidden;
  }

  .nv-card__header {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: var(--nv-card-header);
    padding: 0 var(--nv-space-5);
    border-bottom: 1px solid var(--nv-divider);
  }

  .nv-card--plain .nv-card__header {
    border-bottom: 0;
  }

  .nv-card__title {
    flex-grow: 1;
    min-width: 0;
    font-size: var(--nv-text-md);
    line-height: 20px;
  }

  .nv-card__actions {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  .nv-card__actions a {
    color: var(--nv-text-secondary);
  }

  .nv-card__actions a:hover {
    color: var(--nv-text);
  }

  .nv-card__body {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
    flex-grow: 1;
    min-width: 0;
    padding: var(--nv-space-4) var(--nv-space-5) var(--nv-space-5);
  }

  .nv-card--flush .nv-card__body {
    padding: 0;
    gap: 0;
  }

  .nv-card__footer {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    flex-wrap: wrap;
    min-height: var(--nv-card-header);
    padding: 0 var(--nv-space-5);
    border-top: 1px solid var(--nv-divider);
  }

  @media (min-width: 1024px) {
    .nv-card[data-cols] {
      grid-column: span var(--nv-card-cols);
    }

    .nv-card--span-2 {
      grid-column: span 2;
    }

    .nv-card--span-3 {
      grid-column: span 3;
    }
  }

  @media (min-width: 768px) and (max-width: 1023px) {
    .nv-card[data-cols] {
      grid-column: 1 / -1;
    }

    .nv-card[data-cols="3"],
    .nv-card[data-cols="4"] {
      grid-column: span 3;
    }
  }

  @media (max-width: 767px) {
    .nv-card__header,
    .nv-card__footer {
      padding: 0 var(--nv-space-4);
    }

    .nv-card__body {
      padding: var(--nv-space-4);
    }
  }
}
</style>

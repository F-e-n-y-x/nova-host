<script setup>
/**
 * Key/value list rendered as a <dl> grid.
 *
 * Props: items — array of { term, value, mono?, muted?, key? }; termWidth (CSS length).
 * Slot: value (scoped: { item }) to render custom values.
 */
defineProps({
  items: { type: Array, required: true },
  termWidth: { type: String, default: '112px' },
})
</script>

<template>
  <dl class="nv-dl" :style="{ '--nv-dl-term': termWidth }">
    <template v-for="item in items" :key="item.key || item.term">
      <dt class="nv-dl__term">{{ item.term }}</dt>
      <dd :class="['nv-dl__value', { 'nv-mono': item.mono, 'nv-secondary': item.muted }]">
        <slot name="value" :item="item">{{ item.value }}</slot>
      </dd>
    </template>
  </dl>
</template>

<style>
@layer components {
  .nv-dl {
    display: grid;
    grid-template-columns: var(--nv-dl-term) minmax(0, 1fr);
    column-gap: var(--nv-space-3);
    row-gap: var(--nv-space-2);
    margin: 0;
    font-size: var(--nv-text-sm);
    line-height: 20px;
  }

  .nv-dl__term {
    color: var(--nv-text-muted);
    font-weight: 400;
  }

  .nv-dl__value {
    margin: 0;
    min-width: 0;
    overflow-wrap: anywhere;
    color: var(--nv-text);
  }

  @media (max-width: 599px) {
    .nv-dl {
      grid-template-columns: minmax(0, 1fr);
      row-gap: 2px;
    }

    .nv-dl__value {
      margin-bottom: var(--nv-space-2);
    }
  }
}
</style>

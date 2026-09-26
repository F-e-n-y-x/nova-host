<script setup>
/**
 * Loading placeholder. Hidden from assistive tech; put aria-busy="true" on the region
 * that is loading.
 *
 * Props: width (CSS length, default '100%'), height (default '14px'), lines (repeat as
 *        a paragraph; the last line is shorter), radius (default 6px).
 */
defineProps({
  width: { type: String, default: '100%' },
  height: { type: String, default: '14px' },
  lines: { type: Number, default: 1 },
  radius: { type: String, default: 'var(--nv-radius-sm)' },
})
</script>

<template>
  <span class="nv-skeleton-group" aria-hidden="true">
    <span v-for="n in lines" :key="n" class="nv-skeleton"
          :style="{ width: n === lines && lines > 1 ? '60%' : width, height, borderRadius: radius }"></span>
  </span>
</template>

<style>
@layer components {
  .nv-skeleton-group {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    width: 100%;
  }

  .nv-skeleton {
    display: block;
    background: var(--nv-raised);
    animation: nv-pulse 1.4s ease-in-out infinite;
  }

  @keyframes nv-pulse {
    50% {
      opacity: 0.45;
    }
  }
}
</style>

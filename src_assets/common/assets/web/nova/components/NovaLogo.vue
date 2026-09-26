<script setup>
/**
 * Nova mark (four-point star) with optional wordmark.
 *
 * Props: size (px, default 28), wordmark (show "Nova" next to the mark).
 */
defineProps({
  size: { type: Number, default: 28 },
  wordmark: { type: Boolean, default: true },
})
</script>

<template>
  <span class="nv-logo">
    <svg :width="size" :height="size" viewBox="0 0 28 28" fill="none" aria-hidden="true" focusable="false">
      <path class="nv-logo__star"
            d="M14 2.5c.9 5.6 5.9 10.6 11.5 11.5-5.6.9-10.6 5.9-11.5 11.5-.9-5.6-5.9-10.6-11.5-11.5C8.1 13.1 13.1 8.1 14 2.5z" />
      <circle class="nv-logo__core" cx="14" cy="14" r="2.2" />
    </svg>
    <span v-if="wordmark" class="nv-logo__word">Nova</span>
  </span>
</template>

<style>
@layer components {
  .nv-logo {
    display: inline-flex;
    align-items: center;
    gap: 10px;
    color: var(--nv-text);
  }

  .nv-logo__star {
    fill: var(--nv-accent-text);
  }

  .nv-logo__core {
    fill: var(--nv-sidebar);
  }

  .nv-logo__word {
    font-size: 17px;
    font-weight: 600;
    letter-spacing: 0.01em;
  }
}
</style>

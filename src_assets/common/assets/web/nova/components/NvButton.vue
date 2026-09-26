<script setup>
/**
 * Button. Renders a RouterLink when `to` is set, an <a> when `href` is set,
 * otherwise a native <button>.
 *
 * Props:
 * - variant: 'primary' | 'secondary' | 'ghost' | 'danger' (outlined, for destructive
 *   secondary actions) | 'danger-solid' (filled, for the confirm step of a destructive action)
 * - size: 'md' (40px) | 'sm' (36px)
 * - loading: shows a spinner, sets aria-busy and disables the button
 * - disabled, type ('button' default), to, href, block (full width)
 */
import { computed } from 'vue'

const props = defineProps({
  variant: { type: String, default: 'secondary' },
  size: { type: String, default: 'md' },
  loading: { type: Boolean, default: false },
  disabled: { type: Boolean, default: false },
  type: { type: String, default: 'button' },
  to: { type: [String, Object], default: null },
  href: { type: String, default: null },
  block: { type: Boolean, default: false },
})

const classes = computed(() => [
  'nv-btn',
  `nv-btn--${props.variant}`,
  `nv-btn--${props.size}`,
  { 'nv-btn--block': props.block, 'nv-btn--loading': props.loading },
])
</script>

<template>
  <RouterLink v-if="to" :to="to" :class="classes"><slot /></RouterLink>
  <a v-else-if="href" :href="href" :class="classes"><slot /></a>
  <button v-else :type="type" :class="classes" :disabled="disabled || loading" :aria-busy="loading ? 'true' : null">
    <span v-if="loading" class="nv-btn__spinner" aria-hidden="true"></span>
    <slot />
  </button>
</template>

<style>
@layer components {
  .nv-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: var(--nv-space-2);
    min-height: var(--nv-control-height);
    padding: 0 var(--nv-space-4);
    border-radius: var(--nv-radius-md);
    border: 1px solid transparent;
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    font-weight: 500;
    line-height: 1;
    white-space: nowrap;
    cursor: pointer;
    text-decoration: none;
    transition: background-color 120ms ease, border-color 120ms ease;
  }

  .nv-btn:hover {
    text-decoration: none;
  }

  .nv-btn:disabled {
    cursor: not-allowed;
    opacity: 0.55;
  }

  .nv-btn--sm {
    min-height: var(--nv-control-height-sm);
    padding: 0 var(--nv-space-3);
    font-size: var(--nv-text-sm);
  }

  .nv-btn--block {
    display: flex;
    width: 100%;
  }

  .nv-btn--primary {
    background: var(--nv-accent);
    border-color: var(--nv-accent);
    color: var(--nv-on-accent);
  }

  .nv-btn--primary:hover:not(:disabled) {
    background: var(--nv-accent-hover);
    border-color: var(--nv-accent-hover);
    color: var(--nv-on-accent);
  }

  .nv-btn--secondary {
    border-color: var(--nv-border-strong);
    color: var(--nv-text);
  }

  .nv-btn--secondary:hover:not(:disabled) {
    background: var(--nv-raised);
    color: var(--nv-text);
  }

  .nv-btn--ghost {
    color: var(--nv-accent-text);
  }

  .nv-btn--ghost:hover:not(:disabled) {
    background: var(--nv-accent-tint);
    color: var(--nv-accent-text);
  }

  .nv-btn--danger {
    border-color: var(--nv-danger);
    color: var(--nv-danger);
  }

  .nv-btn--danger:hover:not(:disabled) {
    background: var(--nv-danger-tint);
    color: var(--nv-danger);
  }

  .nv-btn--danger-solid {
    background: var(--nv-danger-fill);
    border-color: var(--nv-danger-fill);
    color: #FFFFFF;
  }

  .nv-btn__spinner {
    width: 14px;
    height: 14px;
    border-radius: 50%;
    border: 2px solid currentColor;
    border-right-color: transparent;
    animation: nv-spin 700ms linear infinite;
  }

  @keyframes nv-spin {
    to {
      transform: rotate(360deg);
    }
  }
}
</style>

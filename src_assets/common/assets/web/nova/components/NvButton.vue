<script setup>
/**
 * Button. Renders a RouterLink when `to` is set, an <a> when `href` is set,
 * otherwise a native <button>. One primary button per view (SPEC §5).
 *
 * Props:
 * - variant: 'primary' | 'secondary' | 'ghost' | 'danger' (outline; opens a confirm, label ends
 *   with "…") | 'danger-solid' (the confirming verb inside a destructive dialog)
 * - size: 'md' (36px; 44px on phones) | 'sm' (32px) | 'lg' (44px)
 * - loading: spinner + aria-busy + disabled
 * - disabled, type ('button' default), to, href, block (full width), icon (lucide component, leading)
 * Exposes: focus()
 */
import { computed, ref } from 'vue'

const props = defineProps({
  variant: { type: String, default: 'secondary' },
  size: { type: String, default: 'md' },
  loading: { type: Boolean, default: false },
  disabled: { type: Boolean, default: false },
  type: { type: String, default: 'button' },
  to: { type: [String, Object], default: null },
  href: { type: String, default: null },
  block: { type: Boolean, default: false },
  icon: { type: [Object, Function], default: null },
})

const root = ref(null)

const classes = computed(() => [
  'nv-btn',
  `nv-btn--${props.variant}`,
  `nv-btn--${props.size}`,
  { 'nv-btn--block': props.block, 'nv-btn--loading': props.loading },
])

defineExpose({
  focus: (options) => (root.value?.$el ?? root.value)?.focus?.(options),
})
</script>

<template>
  <RouterLink v-if="to" ref="root" :to="to" :class="classes">
    <component :is="icon" v-if="icon" :size="16" aria-hidden="true" /><slot />
  </RouterLink>
  <a v-else-if="href" ref="root" :href="href" :class="classes">
    <component :is="icon" v-if="icon" :size="16" aria-hidden="true" /><slot />
  </a>
  <button v-else ref="root" :type="type" :class="classes" :disabled="disabled || loading" :aria-busy="loading ? 'true' : null">
    <span v-if="loading" class="nv-btn__spinner" aria-hidden="true"></span>
    <component :is="icon" v-else-if="icon" :size="16" aria-hidden="true" />
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
    padding: 0 14px;
    border-radius: var(--nv-radius-md);
    border: 1px solid transparent;
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    font-size: var(--nv-text-sm);
    font-weight: 500;
    line-height: 1;
    white-space: nowrap;
    cursor: pointer;
    text-decoration: none;
    transition: background-color 150ms ease-out, border-color 150ms ease-out, color 150ms ease-out;
  }

  .nv-btn:hover {
    text-decoration: none;
  }

  .nv-btn:disabled {
    cursor: not-allowed;
    opacity: 0.5;
  }

  .nv-btn--sm {
    min-height: var(--nv-control-height-sm);
    padding: 0 var(--nv-space-3);
  }

  .nv-btn--lg {
    min-height: var(--nv-control-height-lg);
    padding: 0 var(--nv-space-4);
    font-size: var(--nv-text-md);
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
    color: var(--nv-text-secondary);
  }

  .nv-btn--ghost:hover:not(:disabled) {
    background: var(--nv-raised);
    color: var(--nv-text);
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

  .nv-btn--danger-solid:hover:not(:disabled) {
    filter: brightness(0.92);
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

  @media (max-width: 767px) {
    .nv-btn--md {
      min-height: var(--nv-control-height-lg);
    }
  }
}
</style>

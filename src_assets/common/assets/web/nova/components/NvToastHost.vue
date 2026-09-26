<script setup>
/**
 * Renders the toast queue from ../toast.js. Mount once (the app shell does).
 * Uses a polite live region; danger toasts stay until dismissed.
 */
import { useI18n } from 'vue-i18n'
import { CircleAlert, CircleCheck, Info, TriangleAlert, X } from '@lucide/vue'
import { dismissToast, toasts } from '../toast'

const { t } = useI18n()
const icons = { info: Info, success: CircleCheck, warning: TriangleAlert, danger: CircleAlert }
</script>

<template>
  <div class="nv-toasts" role="status" aria-live="polite" aria-relevant="additions">
    <div v-for="item in toasts" :key="item.id" :class="['nv-toast', `nv-toast--${item.variant}`]">
      <component :is="icons[item.variant] || Info" :size="18" class="nv-toast__icon" aria-hidden="true" />
      <span class="nv-toast__msg">{{ item.message }}</span>
      <button type="button" class="nv-toast__close" :aria-label="t('nova.common.dismiss')" @click="dismissToast(item.id)">
        <X :size="16" aria-hidden="true" />
      </button>
    </div>
  </div>
</template>

<style>
@layer components {
  .nv-toasts {
    position: fixed;
    right: var(--nv-space-6);
    bottom: var(--nv-space-6);
    z-index: 1200;
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
    width: min(400px, calc(100vw - 2 * var(--nv-space-4)));
    pointer-events: none;
  }

  .nv-toast {
    --nv-toast-fg: var(--nv-accent-text);
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-3);
    padding: var(--nv-space-3) var(--nv-space-3) var(--nv-space-3) var(--nv-space-4);
    border-radius: var(--nv-radius-md);
    border: 1px solid var(--nv-border-strong);
    background: var(--nv-raised);
    box-shadow: var(--nv-shadow);
    color: var(--nv-text);
    pointer-events: auto;
  }

  .nv-toast--success {
    --nv-toast-fg: var(--nv-success);
  }

  .nv-toast--warning {
    --nv-toast-fg: var(--nv-warning);
  }

  .nv-toast--danger {
    --nv-toast-fg: var(--nv-danger);
  }

  .nv-toast__icon {
    flex-shrink: 0;
    margin-top: 2px;
    color: var(--nv-toast-fg);
  }

  .nv-toast__msg {
    flex-grow: 1;
  }

  .nv-toast__close {
    flex-shrink: 0;
    width: 28px;
    height: 28px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border: 0;
    border-radius: var(--nv-radius-sm);
    background: transparent;
    color: var(--nv-text-secondary);
    cursor: pointer;
  }

  @media (max-width: 899px) {
    .nv-toasts {
      right: var(--nv-space-4);
      bottom: var(--nv-space-4);
    }
  }
}
</style>

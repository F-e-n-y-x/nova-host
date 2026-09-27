<script setup>
/**
 * Renders the toast queue from ../toast.js. Mount once (the app shell does).
 * Two live regions: polite (info/success) and assertive role="alert" (warning/danger).
 * Hover or focus inside a toast pauses its timer; an optional action button (Undo) runs and dismisses.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { CircleAlert, CircleCheck, Info, TriangleAlert, X } from '@lucide/vue'
import { dismissToast, pauseToast, resumeToast, runToastAction, toasts } from '../toast'

const { t } = useI18n()
const icons = { info: Info, success: CircleCheck, warning: TriangleAlert, danger: CircleAlert }
const urgent = computed(() => toasts.filter((x) => x.variant === 'danger' || x.variant === 'warning'))
const calm = computed(() => toasts.filter((x) => x.variant !== 'danger' && x.variant !== 'warning'))
</script>

<template>
  <div class="nv-toasts">
    <div class="nv-toasts__region" role="alert" aria-live="assertive" aria-relevant="additions">
      <div v-for="item in urgent" :key="item.id" :class="['nv-toast', `nv-toast--${item.variant}`]"
           @mouseenter="pauseToast(item.id)" @mouseleave="resumeToast(item.id)"
           @focusin="pauseToast(item.id)" @focusout="resumeToast(item.id)">
        <component :is="icons[item.variant]" :size="18" class="nv-toast__icon" aria-hidden="true" />
        <span class="nv-toast__msg">{{ item.message }}</span>
        <button v-if="item.action" type="button" class="nv-toast__action" @click="runToastAction(item.id)">{{ item.action.label }}</button>
        <button type="button" class="nv-toast__close" :aria-label="t('nova.common.dismiss')" @click="dismissToast(item.id)">
          <X :size="16" aria-hidden="true" />
        </button>
      </div>
    </div>
    <div class="nv-toasts__region" role="status" aria-live="polite" aria-relevant="additions">
      <div v-for="item in calm" :key="item.id" :class="['nv-toast', `nv-toast--${item.variant}`]"
           @mouseenter="pauseToast(item.id)" @mouseleave="resumeToast(item.id)"
           @focusin="pauseToast(item.id)" @focusout="resumeToast(item.id)">
        <component :is="icons[item.variant] || Info" :size="18" class="nv-toast__icon" aria-hidden="true" />
        <span class="nv-toast__msg">{{ item.message }}</span>
        <button v-if="item.action" type="button" class="nv-toast__action" @click="runToastAction(item.id)">{{ item.action.label }}</button>
        <button type="button" class="nv-toast__close" :aria-label="t('nova.common.dismiss')" @click="dismissToast(item.id)">
          <X :size="16" aria-hidden="true" />
        </button>
      </div>
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
    width: min(400px, calc(100vw - 2 * var(--nv-space-4)));
    pointer-events: none;
  }

  .nv-toasts__region {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-2);
  }

  .nv-toasts__region + .nv-toasts__region {
    margin-top: var(--nv-space-2);
  }

  .nv-toast {
    --nv-toast-fg: var(--nv-accent-text);
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 48px;
    padding: var(--nv-space-2) var(--nv-space-2) var(--nv-space-2) var(--nv-space-4);
    border-radius: var(--nv-radius-xl);
    border: 1px solid var(--nv-chip-border);
    background: var(--nv-raised);
    box-shadow: var(--nv-shadow);
    color: var(--nv-text);
    font-size: var(--nv-text-sm);
    line-height: 18px;
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
    color: var(--nv-toast-fg);
  }

  .nv-toast__msg {
    flex-grow: 1;
    min-width: 0;
    padding: var(--nv-space-1) 0;
  }

  .nv-toast__action {
    flex-shrink: 0;
    min-height: 32px;
    padding: 0 var(--nv-space-3);
    border: 1px solid var(--nv-border-strong);
    border-radius: var(--nv-radius-md);
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    font-weight: 500;
    cursor: pointer;
  }

  .nv-toast__action:hover {
    background: var(--nv-surface);
  }

  .nv-toast__close {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
    width: 32px;
    height: 32px;
    padding: 0;
    border: 0;
    border-radius: var(--nv-radius-md);
    background: transparent;
    color: var(--nv-text-secondary);
    cursor: pointer;
  }

  .nv-toast__close:hover {
    background: var(--nv-surface);
    color: var(--nv-text);
  }

  @media (max-width: 767px) {
    .nv-toasts {
      right: var(--nv-space-4);
      left: var(--nv-space-4);
      bottom: calc(var(--nv-space-4) + env(safe-area-inset-bottom));
      width: auto;
    }
  }
}
</style>

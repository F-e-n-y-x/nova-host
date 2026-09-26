<script setup>
/**
 * Detail panel: a 420px side panel from 1024px wide, a full-screen sheet below that.
 * Traps Tab focus, asks to close on Esc or the close button (the parent decides), and
 * returns focus to whatever opened it.
 *
 * Props: open, title (required; labels the panel).
 * Slots: default (scrolling body), footer (actions; primary last).
 * Emits: close-request.
 */
import { nextTick, onBeforeUnmount, useId, useTemplateRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { X } from '@lucide/vue'

const props = defineProps({
  open: { type: Boolean, default: false },
  title: { type: String, required: true },
})
const emit = defineEmits(['close-request'])
const { t } = useI18n()
const id = useId()
const panel = useTemplateRef('panel')
let returnFocus = null

const FOCUSABLE = 'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), ' +
  'textarea:not([disabled]), summary, [tabindex]:not([tabindex="-1"])'

function onKeydown(event) {
  if (event.key === 'Escape') {
    event.stopPropagation()
    emit('close-request')
    return
  }
  if (event.key !== 'Tab' || !panel.value) return
  const items = [...panel.value.querySelectorAll(FOCUSABLE)]
  if (!items.length) return
  const first = items[0]
  const last = items[items.length - 1]
  if (event.shiftKey && document.activeElement === first) {
    event.preventDefault()
    last.focus()
  } else if (!event.shiftKey && document.activeElement === last) {
    event.preventDefault()
    first.focus()
  }
}

watch(() => props.open, async (isOpen) => {
  if (isOpen) {
    returnFocus = document.activeElement
    await nextTick()
    const target = panel.value?.querySelector('[autofocus]') || panel.value?.querySelector('input') || panel.value
    target?.focus()
  } else if (returnFocus?.isConnected) {
    const el = returnFocus
    returnFocus = null
    await nextTick()
    el.focus()
  }
}, { immediate: true })

onBeforeUnmount(() => {
  if (returnFocus?.isConnected) returnFocus.focus()
})
</script>

<template>
  <Teleport to="body">
    <div v-if="open" class="nv-sheet" @keydown="onKeydown">
      <div class="nv-sheet__backdrop" aria-hidden="true" @click="emit('close-request')"></div>
      <div ref="panel" class="nv-sheet__panel" role="dialog" aria-modal="true" :aria-labelledby="`${id}-title`" tabindex="-1">
        <header class="nv-sheet__header">
          <h2 :id="`${id}-title`" class="nv-sheet__title">{{ title }}</h2>
          <button type="button" class="nv-sheet__close" :aria-label="t('nova.common.close')" @click="emit('close-request')">
            <X :size="18" aria-hidden="true" />
          </button>
        </header>
        <div class="nv-sheet__body"><slot /></div>
        <footer v-if="$slots.footer" class="nv-sheet__footer"><slot name="footer" /></footer>
      </div>
    </div>
  </Teleport>
</template>

<style>
@layer components {
  .nv-sheet {
    position: fixed;
    inset: 0;
    z-index: 1090;
    display: flex;
    justify-content: flex-end;
  }

  .nv-sheet__backdrop {
    position: absolute;
    inset: 0;
    background: rgba(8, 9, 11, 0.5);
  }

  .nv-sheet__panel {
    position: relative;
    display: flex;
    flex-direction: column;
    width: 100%;
    height: 100%;
    background: var(--nv-surface);
    color: var(--nv-text);
    box-shadow: var(--nv-shadow);
  }

  .nv-sheet__panel:focus-visible {
    outline: none;
  }

  .nv-sheet__header {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    padding: var(--nv-space-4) var(--nv-space-5);
    border-bottom: 1px solid var(--nv-border);
  }

  .nv-sheet__title {
    flex-grow: 1;
    min-width: 0;
    font-size: var(--nv-text-lg);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-sheet__close {
    width: 44px;
    height: 44px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border: 0;
    border-radius: var(--nv-radius-md);
    background: transparent;
    color: var(--nv-text-secondary);
    cursor: pointer;
  }

  .nv-sheet__close:hover {
    background: var(--nv-raised);
  }

  .nv-sheet__body {
    flex-grow: 1;
    overflow-y: auto;
    overscroll-behavior: contain;
    padding: var(--nv-space-5);
  }

  .nv-sheet__footer {
    display: flex;
    justify-content: flex-end;
    gap: var(--nv-space-3);
    padding: var(--nv-space-4) var(--nv-space-5);
    border-top: 1px solid var(--nv-border);
  }

  @media (min-width: 1024px) {
    .nv-sheet__panel {
      width: 420px;
      border-left: 1px solid var(--nv-border);
    }

    .nv-sheet__close {
      width: 36px;
      height: 36px;
    }
  }
}
</style>

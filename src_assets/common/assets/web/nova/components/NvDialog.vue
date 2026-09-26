<script setup>
/**
 * Modal dialog. Traps Tab focus inside, closes on Esc and on backdrop click (unless
 * `persistent`), and returns focus to whatever was focused before it opened.
 *
 * v-model:open — boolean.
 * Props: title (required; labels the dialog), description, persistent, size ('md' | 'lg').
 * Slots: default (body), footer (actions; put the primary action last).
 * Emits: close (after any close request, before open becomes false).
 */
import { nextTick, onBeforeUnmount, ref, useId, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { X } from '@lucide/vue'

const open = defineModel('open', { type: Boolean, default: false })
const props = defineProps({
  title: { type: String, required: true },
  description: { type: String, default: '' },
  persistent: { type: Boolean, default: false },
  size: { type: String, default: 'md' },
})
const emit = defineEmits(['close'])

const { t } = useI18n()
const id = useId()
const panel = ref(null)
let returnFocus = null

const FOCUSABLE = 'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), ' +
  'textarea:not([disabled]), [tabindex]:not([tabindex="-1"])'

function focusables() {
  return panel.value ? Array.from(panel.value.querySelectorAll(FOCUSABLE)) : []
}

function close() {
  emit('close')
  open.value = false
}

function onKeydown(event) {
  if (event.key === 'Escape' && !props.persistent) {
    event.stopPropagation()
    close()
    return
  }
  if (event.key !== 'Tab') return
  const items = focusables()
  if (items.length === 0) {
    event.preventDefault()
    panel.value?.focus()
    return
  }
  const first = items[0]
  const last = items[items.length - 1]
  const active = document.activeElement
  if (event.shiftKey && (active === first || active === panel.value)) {
    event.preventDefault()
    last.focus()
  } else if (!event.shiftKey && active === last) {
    event.preventDefault()
    first.focus()
  }
}

watch(open, async (isOpen) => {
  if (isOpen) {
    returnFocus = document.activeElement
    await nextTick()
    const target = panel.value?.querySelector('[autofocus]') || focusables()[0] || panel.value
    target?.focus()
  } else if (returnFocus && typeof returnFocus.focus === 'function') {
    const el = returnFocus
    returnFocus = null
    await nextTick()
    el.focus()
  }
}, { immediate: true })

onBeforeUnmount(() => {
  if (returnFocus && typeof returnFocus.focus === 'function') returnFocus.focus()
})
</script>

<template>
  <Teleport to="body">
    <div v-if="open" class="nv-dialog" @keydown="onKeydown">
      <div class="nv-dialog__backdrop" aria-hidden="true" @click="persistent ? null : close()"></div>
      <div ref="panel" :class="['nv-dialog__panel', `nv-dialog__panel--${size}`]" role="dialog" aria-modal="true"
           :aria-labelledby="`${id}-title`" :aria-describedby="description ? `${id}-desc` : null" tabindex="-1">
        <header class="nv-dialog__header">
          <h2 :id="`${id}-title`" class="nv-dialog__title">{{ title }}</h2>
          <button v-if="!persistent" type="button" class="nv-dialog__close" :aria-label="t('nova.common.close')" @click="close">
            <X :size="18" aria-hidden="true" />
          </button>
        </header>
        <p v-if="description" :id="`${id}-desc`" class="nv-dialog__desc">{{ description }}</p>
        <div class="nv-dialog__body"><slot /></div>
        <footer v-if="$slots.footer" class="nv-dialog__footer"><slot name="footer" /></footer>
      </div>
    </div>
  </Teleport>
</template>

<style>
@layer components {
  .nv-dialog {
    position: fixed;
    inset: 0;
    z-index: 1100;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: var(--nv-space-4);
  }

  .nv-dialog__backdrop {
    position: absolute;
    inset: 0;
    background: rgba(8, 9, 11, 0.6);
  }

  .nv-dialog__panel {
    position: relative;
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
    width: 100%;
    max-width: 520px;
    max-height: calc(100vh - 2 * var(--nv-space-4));
    overflow-y: auto;
    padding: var(--nv-space-6);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-surface);
    border: 1px solid var(--nv-border);
    box-shadow: var(--nv-shadow);
    color: var(--nv-text);
  }

  .nv-dialog__panel--lg {
    max-width: 760px;
  }

  .nv-dialog__panel:focus-visible {
    outline: none;
  }

  .nv-dialog__header {
    display: flex;
    align-items: flex-start;
    gap: var(--nv-space-3);
  }

  .nv-dialog__title {
    flex-grow: 1;
    font-size: var(--nv-text-xl);
  }

  .nv-dialog__close {
    width: 36px;
    height: 36px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border: 0;
    border-radius: var(--nv-radius-md);
    background: transparent;
    color: var(--nv-text-secondary);
    cursor: pointer;
  }

  .nv-dialog__close:hover {
    background: var(--nv-raised);
  }

  .nv-dialog__desc {
    color: var(--nv-text-secondary);
  }

  .nv-dialog__footer {
    display: flex;
    justify-content: flex-end;
    flex-wrap: wrap;
    gap: var(--nv-space-3);
  }
}
</style>

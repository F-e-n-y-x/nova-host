<script setup>
/**
 * Modal dialog (SPEC §5: 480 wide, radius 12). Traps Tab inside, closes on Esc and backdrop click
 * (unless `persistent`), locks page scroll, and returns focus to what was focused before it opened.
 * Header and footer stay put while the body scrolls, so the actions are always visible.
 *
 * v-model:open — boolean.
 * Props: title (required; a question naming the object for confirms), description,
 *        persistent, size ('sm' 480 | 'md' 560 | 'lg' 760),
 *        initialFocus (CSS selector inside the dialog, e.g. '[data-nv-cancel]'; default: [autofocus],
 *        then the first control after the close button),
 *        beforeClose (function → boolean | Promise<boolean>; return false to keep it open, e.g. to
 *        confirm discarding edits).
 * Slots: default (body), footer (actions; primary last).
 * Emits: close (after a close request is accepted, before open becomes false).
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
  initialFocus: { type: String, default: '' },
  beforeClose: { type: Function, default: null },
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

async function close() {
  if (props.beforeClose && (await props.beforeClose()) === false) return
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

function initialTarget() {
  const el = panel.value
  if (!el) return null
  if (props.initialFocus) {
    const chosen = el.querySelector(props.initialFocus)
    if (chosen) return chosen
  }
  return el.querySelector('[autofocus]') ||
    focusables().find((node) => !node.classList.contains('nv-dialog__close')) || focusables()[0] || el
}

watch(open, async (isOpen) => {
  document.documentElement.classList.toggle('nv-scroll-locked', isOpen)
  if (isOpen) {
    returnFocus = document.activeElement
    await nextTick()
    initialTarget()?.focus()
  } else if (returnFocus && typeof returnFocus.focus === 'function') {
    const el = returnFocus
    returnFocus = null
    await nextTick()
    el.focus()
  }
}, { immediate: true })

onBeforeUnmount(() => {
  if (open.value) document.documentElement.classList.remove('nv-scroll-locked')
  if (returnFocus && typeof returnFocus.focus === 'function') returnFocus.focus()
})

defineExpose({ close })
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
        <div class="nv-dialog__body">
          <p v-if="description" :id="`${id}-desc`" class="nv-dialog__desc">{{ description }}</p>
          <slot />
        </div>
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
    background: var(--nv-scrim);
    animation: nv-fade 200ms ease-out;
  }

  .nv-dialog__panel {
    position: relative;
    display: flex;
    flex-direction: column;
    width: 100%;
    max-width: 560px;
    max-height: min(720px, calc(100vh - 2 * var(--nv-space-4)));
    border-radius: var(--nv-radius-xl);
    border: 1px solid var(--nv-border);
    background: var(--nv-surface);
    box-shadow: var(--nv-shadow);
    color: var(--nv-text);
    animation: nv-rise 200ms var(--nv-ease);
  }

  .nv-dialog__panel--sm {
    max-width: 480px;
  }

  .nv-dialog__panel--lg {
    max-width: 760px;
  }

  .nv-dialog__panel:focus-visible {
    outline: none;
  }

  .nv-dialog__header {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 60px;
    padding: 0 var(--nv-space-3) 0 var(--nv-space-6);
    flex-shrink: 0;
  }

  .nv-dialog__title {
    flex-grow: 1;
    min-width: 0;
    font-size: var(--nv-text-lg);
    line-height: 22px;
  }

  .nv-dialog__close {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 32px;
    height: 32px;
    flex-shrink: 0;
    padding: 0;
    border: 0;
    border-radius: var(--nv-radius-md);
    background: transparent;
    color: var(--nv-text-secondary);
    cursor: pointer;
  }

  .nv-dialog__close:hover {
    background: var(--nv-raised);
    color: var(--nv-text);
  }

  .nv-dialog__body {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
    min-height: 0;
    overflow-y: auto;
    overscroll-behavior: contain;
    padding: 0 var(--nv-space-6) var(--nv-space-5);
  }

  .nv-dialog__desc {
    font-size: var(--nv-text-md);
    line-height: 20px;
    color: var(--nv-text-secondary);
  }

  .nv-dialog__footer {
    display: flex;
    flex-wrap: wrap;
    justify-content: flex-end;
    gap: var(--nv-space-3);
    flex-shrink: 0;
    padding: var(--nv-space-4) var(--nv-space-6);
    border-top: 1px solid var(--nv-divider);
  }

  @keyframes nv-fade {
    from {
      opacity: 0;
    }
  }

  @keyframes nv-rise {
    from {
      opacity: 0;
      transform: translateY(8px);
    }
  }

  @media (max-width: 767px) {
    .nv-dialog {
      align-items: flex-end;
      padding: 0;
    }

    .nv-dialog__panel {
      max-width: none;
      max-height: 92vh;
      border-radius: var(--nv-radius-xl) var(--nv-radius-xl) 0 0;
      padding-bottom: env(safe-area-inset-bottom);
    }

    .nv-dialog__footer > * {
      flex-grow: 1;
    }
  }
}
</style>

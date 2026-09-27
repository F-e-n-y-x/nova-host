<script setup>
/**
 * Side panel / sheet (SPEC §5 "Side panel" and "Add games sheet"). From 1024px it slides in from
 * the right (440 wide, or 680 for size="lg") over a scrim; below 1024px it is a full-screen sheet.
 * Modal: traps Tab, Esc closes, locks page scroll, returns focus to the opener.
 *
 * Route-addressable pattern (Back closes it):
 *   const open = computed({ get: () => Boolean(route.params.id), set: (v) => { if (!v) router.push('/library') } })
 *   <NvSheet v-model:open="open" :title="app.name"> … </NvSheet>
 * and navigate to `/library/<id>` from the row. Focus returns to whatever opened it; if the row was
 * re-rendered, pass `returnFocus` (an element or a function returning one).
 *
 * v-model:open — boolean.
 * Props: title (required), subtitle, size ('md' 440 | 'lg' 680), beforeClose (→ boolean |
 *        Promise<boolean>; false keeps it open), returnFocus, initialFocus (selector), bodyClass.
 * Slots: default (body), header-actions (next to close), footer-start (⋯ / secondary, left),
 *        footer (Cancel + primary, right).
 * Emits: close.
 */
import { nextTick, onBeforeUnmount, ref, useId, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { X } from '@lucide/vue'

const open = defineModel('open', { type: Boolean, default: false })
const props = defineProps({
  title: { type: String, required: true },
  subtitle: { type: String, default: '' },
  size: { type: String, default: 'md' },
  beforeClose: { type: Function, default: null },
  returnFocus: { type: [Object, Function], default: null },
  initialFocus: { type: String, default: '' },
  bodyClass: { type: String, default: '' },
})
const emit = defineEmits(['close'])

const { t } = useI18n()
const id = useId()
const panel = ref(null)
let opener = null

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
  if (event.key === 'Escape') {
    event.stopPropagation()
    close()
    return
  }
  if (event.key !== 'Tab') return
  const items = focusables()
  if (items.length === 0) return
  const first = items[0]
  const last = items[items.length - 1]
  if (event.shiftKey && (document.activeElement === first || document.activeElement === panel.value)) {
    event.preventDefault()
    last.focus()
  } else if (!event.shiftKey && document.activeElement === last) {
    event.preventDefault()
    first.focus()
  }
}

function resolveReturn() {
  const target = typeof props.returnFocus === 'function' ? props.returnFocus() : props.returnFocus
  return target?.$el ?? target ?? opener
}

watch(open, async (isOpen) => {
  document.documentElement.classList.toggle('nv-scroll-locked', isOpen)
  if (isOpen) {
    opener = document.activeElement
    await nextTick()
    const el = panel.value
    const target = (props.initialFocus && el?.querySelector(props.initialFocus)) || el?.querySelector('[autofocus]') || el
    target?.focus()
  } else {
    const target = resolveReturn()
    opener = null
    await nextTick()
    if (target && typeof target.focus === 'function' && target.isConnected !== false) target.focus()
  }
}, { immediate: true })

onBeforeUnmount(() => {
  if (open.value) document.documentElement.classList.remove('nv-scroll-locked')
})

defineExpose({ close })
</script>

<template>
  <Teleport to="body">
    <div v-if="open" :class="['nv-sheet', `nv-sheet--${size}`]" @keydown="onKeydown">
      <div class="nv-sheet__scrim" aria-hidden="true" @click="close"></div>
      <div ref="panel" class="nv-sheet__panel" role="dialog" aria-modal="true" :aria-labelledby="`${id}-title`"
           :aria-describedby="subtitle ? `${id}-sub` : null" tabindex="-1">
        <header class="nv-sheet__header">
          <div class="nv-sheet__heading">
            <h2 :id="`${id}-title`" class="nv-sheet__title">{{ title }}</h2>
            <p v-if="subtitle" :id="`${id}-sub`" class="nv-sheet__subtitle">{{ subtitle }}</p>
          </div>
          <slot name="header-actions" />
          <button type="button" class="nv-sheet__close" :aria-label="t('nova.common.close')" @click="close">
            <X :size="18" aria-hidden="true" />
          </button>
        </header>
        <div :class="['nv-sheet__body', bodyClass]"><slot /></div>
        <footer v-if="$slots.footer || $slots['footer-start']" class="nv-sheet__footer">
          <div class="nv-sheet__footer-start"><slot name="footer-start" /></div>
          <div class="nv-sheet__footer-end"><slot name="footer" /></div>
        </footer>
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

  .nv-sheet__scrim {
    position: absolute;
    inset: 0;
    background: var(--nv-scrim);
    animation: nv-fade 200ms ease-out;
  }

  .nv-sheet__panel {
    position: relative;
    display: flex;
    flex-direction: column;
    width: 440px;
    max-width: 100%;
    height: 100%;
    border-left: 1px solid var(--nv-border);
    background: var(--nv-panel);
    color: var(--nv-text);
    box-shadow: var(--nv-shadow);
    animation: nv-slide-in 200ms var(--nv-ease);
  }

  .nv-sheet--lg .nv-sheet__panel {
    width: 680px;
  }

  .nv-sheet__panel:focus-visible {
    outline: none;
  }

  .nv-sheet__header {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    min-height: 64px;
    padding: 0 var(--nv-space-3) 0 var(--nv-space-6);
    border-bottom: 1px solid var(--nv-divider);
    flex-shrink: 0;
  }

  .nv-sheet__heading {
    display: flex;
    flex-direction: column;
    flex-grow: 1;
    min-width: 0;
  }

  .nv-sheet__title {
    font-size: var(--nv-text-lg);
    line-height: 22px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-sheet__subtitle {
    font-size: var(--nv-text-xs);
    line-height: 16px;
    color: var(--nv-text-muted);
  }

  .nv-sheet__close {
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

  .nv-sheet__close:hover {
    background: var(--nv-raised);
    color: var(--nv-text);
  }

  .nv-sheet__body {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-5);
    flex-grow: 1;
    min-height: 0;
    overflow-y: auto;
    overscroll-behavior: contain;
    padding: var(--nv-space-5) var(--nv-space-6);
  }

  .nv-sheet__footer {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 64px;
    padding: 0 var(--nv-space-6);
    border-top: 1px solid var(--nv-divider);
    flex-shrink: 0;
  }

  .nv-sheet__footer-start {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    flex-grow: 1;
    min-width: 0;
    color: var(--nv-text-muted);
    font-size: var(--nv-text-sm);
  }

  .nv-sheet__footer-end {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
  }

  @keyframes nv-slide-in {
    from {
      opacity: 0;
      transform: translateX(16px);
    }
  }

  @media (max-width: 1023px) {
    .nv-sheet__panel,
    .nv-sheet--lg .nv-sheet__panel {
      width: 100%;
      border-left: 0;
    }
  }

  @media (max-width: 767px) {
    .nv-sheet__header {
      min-height: 56px;
      padding: 0 var(--nv-space-2) 0 var(--nv-space-4);
    }

    .nv-sheet__close {
      width: 44px;
      height: 44px;
    }

    .nv-sheet__body {
      padding: var(--nv-space-4);
    }

    .nv-sheet__footer {
      padding: var(--nv-space-2) var(--nv-space-4) calc(var(--nv-space-2) + env(safe-area-inset-bottom));
    }
  }
}
</style>

<script setup>
/**
 * "⋯" action menu (promoted from the Applications page). Opens a role="menu" list positioned
 * against the trigger (teleported, so cards with overflow:hidden don't clip it).
 * Up/Down/Home/End move, Enter/Space choose, Esc returns focus to the trigger, Tab or an outside
 * click closes.
 *
 * Props: label (required; accessible name of the trigger), items — array of
 *        { id, label, icon?, danger?, disabled?, divider? (renders a separator), checked? (true/false
 *        makes it a menuitemradio), onSelect? }, align ('end' | 'start'), triggerClass,
 *        size ('md' 32px | 'sm' 28px).
 * Slots: trigger (custom trigger content; default is the ⋯ icon).
 * Emits: select(id).
 */
import { computed, nextTick, onBeforeUnmount, ref, shallowRef, useId } from 'vue'
import { Check, Ellipsis } from '@lucide/vue'

const props = defineProps({
  label: { type: String, required: true },
  items: { type: Array, required: true },
  align: { type: String, default: 'end' },
  triggerClass: { type: String, default: '' },
  size: { type: String, default: 'md' },
})
const emit = defineEmits(['select'])
const id = useId()
const open = shallowRef(false)
const button = ref(null)
const list = ref(null)
const position = ref({ top: 0, left: 0 })
const hasRadios = computed(() => props.items.some((item) => typeof item.checked === 'boolean'))

function menuItems() {
  return [...(list.value?.querySelectorAll('[role^="menuitem"]:not([disabled])') ?? [])]
}

function onOutside(event) {
  if (!list.value?.contains(event.target) && !button.value?.contains(event.target)) close(false)
}

function place() {
  const rect = button.value?.getBoundingClientRect()
  if (!rect) return
  const width = list.value?.offsetWidth || 200
  const height = list.value?.offsetHeight || 0
  let left = props.align === 'start' ? rect.left : rect.right - width
  left = Math.max(8, Math.min(left, window.innerWidth - width - 8))
  let top = rect.bottom + 4
  if (height && top + height > window.innerHeight - 8) top = Math.max(8, rect.top - height - 4)
  position.value = { top, left }
}

async function show(focusLast = false) {
  open.value = true
  document.addEventListener('pointerdown', onOutside, true)
  window.addEventListener('resize', place)
  window.addEventListener('scroll', place, true)
  await nextTick()
  place()
  const items = menuItems()
  ;(focusLast ? items.at(-1) : items[0])?.focus()
}

function close(restoreFocus = true) {
  if (!open.value) return
  open.value = false
  document.removeEventListener('pointerdown', onOutside, true)
  window.removeEventListener('resize', place)
  window.removeEventListener('scroll', place, true)
  if (restoreFocus) button.value?.focus()
}

function onButtonKeydown(event) {
  if (event.key === 'ArrowDown') { event.preventDefault(); show() }
  if (event.key === 'ArrowUp') { event.preventDefault(); show(true) }
}

function onMenuKeydown(event) {
  const items = menuItems()
  const i = items.indexOf(document.activeElement)
  const moves = { ArrowDown: i + 1, ArrowUp: i - 1, Home: 0, End: items.length - 1 }
  if (event.key in moves) {
    event.preventDefault()
    items[(moves[event.key] + items.length) % items.length]?.focus()
  } else if (event.key === 'Escape') {
    event.preventDefault()
    event.stopPropagation()
    close()
  } else if (event.key === 'Tab') {
    close(false)
  }
}

function choose(item) {
  if (item.disabled) return
  close()
  emit('select', item.id)
  if (typeof item.onSelect === 'function') item.onSelect()
}

onBeforeUnmount(() => close(false))
defineExpose({ show, close })
</script>

<template>
  <span class="nv-menu">
    <button ref="button" type="button" :class="['nv-menu__button', `nv-menu__button--${size}`, triggerClass]"
            :aria-label="label" :title="$slots.trigger ? null : label" aria-haspopup="menu"
            :aria-expanded="open ? 'true' : 'false'" :aria-controls="open ? `${id}-menu` : null"
            @click="open ? close() : show()" @keydown="onButtonKeydown">
      <slot name="trigger"><Ellipsis :size="18" aria-hidden="true" /></slot>
    </button>
    <Teleport to="body">
      <ul v-if="open" :id="`${id}-menu`" ref="list" role="menu" :aria-label="label" class="nv-menu__list"
          :style="{ top: `${position.top}px`, left: `${position.left}px` }" @keydown="onMenuKeydown">
        <template v-for="(item, i) in items" :key="item.id ?? `sep-${i}`">
          <li v-if="item.divider" role="separator" class="nv-menu__sep"></li>
          <li v-else role="none">
            <button type="button" :role="typeof item.checked === 'boolean' ? 'menuitemradio' : 'menuitem'"
                    :aria-checked="typeof item.checked === 'boolean' ? String(item.checked) : null"
                    tabindex="-1" :disabled="item.disabled"
                    :class="['nv-menu__item', { 'nv-menu__item--danger': item.danger }]" @click="choose(item)">
              <span v-if="hasRadios" class="nv-menu__check" aria-hidden="true"><Check v-if="item.checked" :size="14" /></span>
              <component :is="item.icon" v-if="item.icon" :size="16" aria-hidden="true" />
              <span class="nv-menu__label">{{ item.label }}</span>
            </button>
          </li>
        </template>
      </ul>
    </Teleport>
  </span>
</template>

<style>
@layer components {
  .nv-menu {
    position: relative;
    display: inline-flex;
  }

  .nv-menu__button {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: var(--nv-space-2);
    min-width: 32px;
    height: 32px;
    padding: 0 6px;
    border: 1px solid transparent;
    border-radius: var(--nv-radius-md);
    background: transparent;
    color: var(--nv-text-secondary);
    font: inherit;
    cursor: pointer;
  }

  .nv-menu__button--sm {
    min-width: 28px;
    height: 28px;
  }

  .nv-menu__button:hover,
  .nv-menu__button[aria-expanded="true"] {
    background: var(--nv-raised);
    color: var(--nv-text);
  }

  .nv-menu__list {
    position: fixed;
    z-index: 1150;
    min-width: 200px;
    max-width: 320px;
    margin: 0;
    padding: var(--nv-space-1);
    list-style: none;
    border-radius: var(--nv-radius-lg);
    border: 1px solid var(--nv-chip-border);
    background: var(--nv-raised);
    box-shadow: var(--nv-shadow);
    animation: nv-fade 150ms ease-out;
  }

  .nv-menu__sep {
    height: 1px;
    margin: var(--nv-space-1) 0;
    background: var(--nv-border);
  }

  .nv-menu__item {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    width: 100%;
    min-height: 34px;
    padding: 0 var(--nv-space-3) 0 var(--nv-space-2);
    border: 0;
    border-radius: var(--nv-radius-sm);
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    font-size: var(--nv-text-sm);
    text-align: left;
    cursor: pointer;
  }

  .nv-menu__item:hover:not(:disabled),
  .nv-menu__item:focus-visible {
    background: var(--nv-surface);
    outline: none;
  }

  .nv-menu__item:focus-visible {
    box-shadow: inset 0 0 0 2px var(--nv-focus);
  }

  .nv-menu__item:disabled {
    cursor: not-allowed;
    opacity: 0.5;
  }

  .nv-menu__item--danger {
    color: var(--nv-danger);
  }

  .nv-menu__check {
    display: inline-flex;
    width: 14px;
    color: var(--nv-accent-text);
  }

  .nv-menu__label {
    flex-grow: 1;
    min-width: 0;
  }

  @media (max-width: 767px) {
    .nv-menu__button {
      min-width: 44px;
      height: 44px;
    }

    .nv-menu__item {
      min-height: 44px;
    }
  }
}
</style>

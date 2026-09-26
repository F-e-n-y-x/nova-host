<script setup>
/**
 * "⋯" menu button. Opens a role="menu" list; Up/Down/Home/End move between items,
 * Esc or Tab closes it and returns focus to the button, clicking outside closes it.
 *
 * Props: label (required; the button's accessible name), items (array of
 *        { id, label, danger?, icon? }), align ('end' | 'start').
 * Emits: select(id).
 */
import { nextTick, onBeforeUnmount, shallowRef, useId, useTemplateRef } from 'vue'
import { Ellipsis } from '@lucide/vue'

const props = defineProps({
  label: { type: String, required: true },
  items: { type: Array, required: true },
  align: { type: String, default: 'end' },
})
const emit = defineEmits(['select'])
const id = useId()
const open = shallowRef(false)
const root = useTemplateRef('root')
const button = useTemplateRef('button')

function menuItems() {
  return [...(root.value?.querySelectorAll('[role="menuitem"]') ?? [])]
}

function onOutside(event) {
  if (!root.value?.contains(event.target)) close(false)
}

async function show(focusLast = false) {
  open.value = true
  document.addEventListener('pointerdown', onOutside, true)
  await nextTick()
  const items = menuItems()
  ;(focusLast ? items.at(-1) : items[0])?.focus()
}

function close(restoreFocus = true) {
  if (!open.value) return
  open.value = false
  document.removeEventListener('pointerdown', onOutside, true)
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
  close()
  emit('select', item.id)
}

onBeforeUnmount(() => document.removeEventListener('pointerdown', onOutside, true))
</script>

<template>
  <div ref="root" class="nv-menu">
    <button ref="button" type="button" class="nv-menu__button" :aria-label="label" :title="label" aria-haspopup="menu"
            :aria-expanded="open ? 'true' : 'false'" :aria-controls="open ? `${id}-menu` : null"
            @click="open ? close() : show()" @keydown="onButtonKeydown">
      <Ellipsis :size="18" aria-hidden="true" />
    </button>
    <ul v-if="open" :id="`${id}-menu`" role="menu" :aria-label="label" :class="['nv-menu__list', `nv-menu__list--${align}`]"
        @keydown="onMenuKeydown">
      <li v-for="item in items" :key="item.id" role="none">
        <button type="button" role="menuitem" tabindex="-1" :class="['nv-menu__item', { 'nv-menu__item--danger': item.danger }]"
                @click="choose(item)">
          <component :is="item.icon" v-if="item.icon" :size="16" aria-hidden="true" />
          {{ item.label }}
        </button>
      </li>
    </ul>
  </div>
</template>

<style>
@layer components {
  .nv-menu {
    position: relative;
    display: inline-flex;
  }

  .nv-menu__button {
    width: var(--nv-control-height-sm);
    height: var(--nv-control-height-sm);
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border: 1px solid transparent;
    border-radius: var(--nv-radius-md);
    background: transparent;
    color: var(--nv-text-secondary);
    cursor: pointer;
  }

  .nv-menu__button:hover,
  .nv-menu__button[aria-expanded="true"] {
    background: var(--nv-raised);
    color: var(--nv-text);
  }

  .nv-menu__list {
    position: absolute;
    top: calc(100% + var(--nv-space-1));
    z-index: 20;
    min-width: 180px;
    margin: 0;
    padding: var(--nv-space-1);
    list-style: none;
    border-radius: var(--nv-radius-md);
    border: 1px solid var(--nv-border-strong);
    background: var(--nv-surface);
    box-shadow: var(--nv-shadow);
  }

  .nv-menu__list--end {
    right: 0;
  }

  .nv-menu__list--start {
    left: 0;
  }

  .nv-menu__item {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    width: 100%;
    min-height: 40px;
    padding: 0 var(--nv-space-3);
    border: 0;
    border-radius: var(--nv-radius-sm);
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    text-align: left;
    white-space: nowrap;
    cursor: pointer;
  }

  .nv-menu__item:hover,
  .nv-menu__item:focus-visible {
    background: var(--nv-raised);
  }

  .nv-menu__item--danger {
    color: var(--nv-danger);
  }
}
</style>

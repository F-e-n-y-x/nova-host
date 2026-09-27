<script setup>
/**
 * Command palette (Ctrl/⌘K or the top bar search field). A modal combobox + listbox: type to search
 * every provider in ../search.js, Up/Down to move, Enter to open, Esc to close (focus returns).
 * Mounted once by the app shell.
 */
import { computed, nextTick, ref, watch } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { CornerDownLeft, Search } from '@lucide/vue'
import { closePalette, palette, runSearch } from '../search'

const { t, te } = useI18n()
const router = useRouter()
const input = ref(null)
const groups = ref([])
const active = ref(0)
const loading = ref(false)
let returnFocus = null
let seq = 0
let debounce = null

const flat = computed(() => groups.value.flatMap((g) => g.items))
const activeId = computed(() => (flat.value[active.value] ? `nv-cmd-${flat.value[active.value].id}` : null))

async function search() {
  const mine = ++seq
  loading.value = true
  const result = await runSearch(palette.query, { t, te })
  if (mine !== seq) return
  groups.value = result
  active.value = 0
  loading.value = false
}

watch(() => palette.query, () => {
  clearTimeout(debounce)
  debounce = setTimeout(search, 120)
})

watch(() => palette.open, async (open) => {
  document.documentElement.classList.toggle('nv-scroll-locked', open)
  if (open) {
    returnFocus = document.activeElement
    search()
    await nextTick()
    input.value?.focus()
    input.value?.select()
  } else if (returnFocus?.focus) {
    const el = returnFocus
    returnFocus = null
    await nextTick()
    el.focus()
  }
})

function choose(item) {
  if (!item) return
  closePalette()
  if (item.to) router.push(item.to)
  else if (typeof item.run === 'function') item.run()
}

function onKeydown(event) {
  const count = flat.value.length
  if (event.key === 'ArrowDown' && count) {
    event.preventDefault()
    active.value = (active.value + 1) % count
  } else if (event.key === 'ArrowUp' && count) {
    event.preventDefault()
    active.value = (active.value - 1 + count) % count
  } else if (event.key === 'Enter') {
    event.preventDefault()
    choose(flat.value[active.value])
  } else if (event.key === 'Escape') {
    event.preventDefault()
    closePalette()
  } else if (event.key === 'Tab') {
    event.preventDefault()
  }
}

watch(activeId, async (id) => {
  await nextTick()
  if (id) document.getElementById(id)?.scrollIntoView?.({ block: 'nearest' })
})

function indexOf(item) {
  return flat.value.indexOf(item)
}
</script>

<template>
  <Teleport to="body">
    <div v-if="palette.open" class="nv-cmd">
      <div class="nv-cmd__scrim" aria-hidden="true" @click="closePalette"></div>
      <div class="nv-cmd__panel" role="dialog" aria-modal="true" :aria-label="t('nova.search.dialog_label')">
        <div class="nv-cmd__field">
          <Search :size="16" aria-hidden="true" class="nv-cmd__icon" />
          <input ref="input" v-model="palette.query" class="nv-cmd__input" type="text" role="combobox"
                 aria-autocomplete="list" aria-expanded="true" aria-controls="nv-cmd-list"
                 :aria-activedescendant="activeId" :placeholder="t('nova.search.placeholder')"
                 :aria-label="t('nova.search.placeholder')" spellcheck="false" autocomplete="off" @keydown="onKeydown">
          <kbd class="nv-cmd__kbd">Esc</kbd>
        </div>
        <div id="nv-cmd-list" class="nv-cmd__list" role="listbox" :aria-label="t('nova.search.results')" :aria-busy="loading ? 'true' : null">
          <template v-for="group in groups" :key="group.id">
            <div class="nv-cmd__group" role="presentation">{{ group.group }}</div>
            <div v-for="item in group.items" :id="`nv-cmd-${item.id}`" :key="item.id" role="option"
                 :aria-selected="indexOf(item) === active ? 'true' : 'false'"
                 :class="['nv-cmd__item', { 'nv-cmd__item--active': indexOf(item) === active }]"
                 @mousemove="active = indexOf(item)" @click="choose(item)">
              <component :is="item.icon" v-if="item.icon" :size="16" aria-hidden="true" class="nv-cmd__item-icon" />
              <span class="nv-cmd__item-text">
                <span class="nv-cmd__item-label">{{ item.label }}</span>
                <span v-if="item.sub" class="nv-cmd__item-sub">{{ item.sub }}</span>
              </span>
              <CornerDownLeft v-if="indexOf(item) === active" :size="14" aria-hidden="true" class="nv-cmd__enter" />
            </div>
          </template>
          <p v-if="!loading && flat.length === 0" class="nv-cmd__empty" role="status">
            {{ palette.query ? t('nova.search.no_results', { q: palette.query }) : t('nova.search.hint') }}
          </p>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<style>
@layer components {
  .nv-cmd {
    position: fixed;
    inset: 0;
    z-index: 1160;
    display: flex;
    justify-content: center;
    align-items: flex-start;
    padding: 12vh var(--nv-space-4) var(--nv-space-4);
  }

  .nv-cmd__scrim {
    position: absolute;
    inset: 0;
    background: var(--nv-scrim);
    animation: nv-fade 150ms ease-out;
  }

  .nv-cmd__panel {
    position: relative;
    display: flex;
    flex-direction: column;
    width: 100%;
    max-width: 600px;
    max-height: 70vh;
    border-radius: var(--nv-radius-xl);
    border: 1px solid var(--nv-chip-border);
    background: var(--nv-surface);
    box-shadow: var(--nv-shadow);
    overflow: hidden;
    animation: nv-rise 150ms var(--nv-ease);
  }

  .nv-cmd__field {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 52px;
    padding: 0 var(--nv-space-4);
    border-bottom: 1px solid var(--nv-divider);
  }

  .nv-cmd__icon {
    flex-shrink: 0;
    color: var(--nv-text-muted);
  }

  .nv-cmd__input {
    flex-grow: 1;
    min-width: 0;
    border: 0;
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    font-size: var(--nv-text-lg);
  }

  .nv-cmd__input:focus-visible {
    outline: none;
  }

  .nv-cmd__kbd,
  .nv-topsearch__kbd {
    padding: 2px 6px;
    border-radius: 4px;
    border: 1px solid var(--nv-chip-border);
    color: var(--nv-text-muted);
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-2xs);
    line-height: 14px;
  }

  .nv-cmd__list {
    overflow-y: auto;
    padding: var(--nv-space-2);
  }

  .nv-cmd__group {
    padding: var(--nv-space-3) var(--nv-space-3) var(--nv-space-1);
    font-size: var(--nv-text-2xs);
    font-weight: 500;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--nv-text-muted);
  }

  .nv-cmd__item {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 44px;
    padding: 0 var(--nv-space-3);
    border-radius: var(--nv-radius-md);
    color: var(--nv-text);
    cursor: pointer;
  }

  .nv-cmd__item--active {
    background: var(--nv-raised);
  }

  .nv-cmd__item-icon {
    flex-shrink: 0;
    color: var(--nv-text-secondary);
  }

  .nv-cmd__item-text {
    display: flex;
    flex-direction: column;
    flex-grow: 1;
    min-width: 0;
    line-height: 18px;
  }

  .nv-cmd__item-label {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-cmd__item-sub {
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-cmd__enter {
    color: var(--nv-text-muted);
  }

  .nv-cmd__empty {
    padding: var(--nv-space-6) var(--nv-space-3);
    text-align: center;
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  @media (max-width: 767px) {
    .nv-cmd {
      padding: 0;
    }

    .nv-cmd__panel {
      max-width: none;
      max-height: 100vh;
      height: 100%;
      border-radius: 0;
    }
  }
}
</style>

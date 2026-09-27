<script setup>
/**
 * Page frame. Inside the app shell the title becomes the top bar's <h1>, and the `subtitle` and
 * `actions` slots are teleported into the top bar (SPEC §4: one primary action, right). The body
 * gets the shared padding (24 / 32 / 28; 16 on phone) and a 16px gap between blocks.
 * Without the shell (unit tests, bare pages) it renders its own header instead.
 *
 * Props: title (required; the page's only <h1>), wide (kept for compatibility; pages are full width),
 *        grid (body becomes the 12-column grid — children use NvCard :cols="N"),
 *        hideSearch (hide the top bar's global search button, for pages with their own search;
 *        Ctrl/⌘K still opens the palette).
 * Slots: subtitle (short, muted, next to the title — hidden on phones), actions (the primary action,
 *        optionally one secondary / NvActionMenu before it), default (body).
 */
import { inject, onBeforeUnmount, watchEffect } from 'vue'

const props = defineProps({
  title: { type: String, required: true },
  wide: { type: Boolean, default: false },
  grid: { type: Boolean, default: false },
  hideSearch: { type: Boolean, default: false },
})

const shell = inject('nvShell', null)

watchEffect(() => {
  if (typeof document !== 'undefined') document.title = `${props.title} · Nova`
  if (shell) {
    shell.page.title = props.title
    shell.page.hideSearch = props.hideSearch
  }
})

onBeforeUnmount(() => {
  if (shell && shell.page.title === props.title) {
    shell.page.title = ''
    shell.page.hideSearch = false
  }
})
</script>

<template>
  <div :class="['nv-page', { 'nv-page--grid': grid }]">
    <template v-if="shell">
      <Teleport v-if="$slots.subtitle" defer to="#nv-topbar-sub"><slot name="subtitle" /></Teleport>
      <Teleport v-if="$slots.actions" defer to="#nv-topbar-actions"><slot name="actions" /></Teleport>
    </template>
    <header v-else class="nv-page__header">
      <div class="nv-page__heading">
        <h1>{{ title }}</h1>
        <div v-if="$slots.subtitle" class="nv-page__subtitle"><slot name="subtitle" /></div>
      </div>
      <div v-if="$slots.actions" class="nv-page__actions"><slot name="actions" /></div>
    </header>
    <slot />
  </div>
</template>

<style>
@layer components {
  .nv-page {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
    min-width: 0;
    padding: var(--nv-space-6) var(--nv-space-8) var(--nv-space-7);
  }

  .nv-page--grid {
    display: grid;
    grid-template-columns: repeat(12, minmax(0, 1fr));
    align-items: stretch;
  }

  .nv-page--grid > * {
    grid-column: 1 / -1;
  }

  .nv-page__header {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: var(--nv-space-4);
  }

  .nv-page__heading {
    display: flex;
    align-items: baseline;
    gap: var(--nv-space-3);
    flex-grow: 1;
    min-width: 0;
  }

  .nv-page__subtitle {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-page__actions {
    display: flex;
    gap: var(--nv-space-2);
  }

  @media (min-width: 768px) and (max-width: 1279px) {
    .nv-page--grid {
      grid-template-columns: repeat(6, minmax(0, 1fr));
    }
  }

  @media (min-width: 768px) and (max-width: 1023px) {
    .nv-page {
      padding: var(--nv-space-5) var(--nv-space-6) var(--nv-space-6);
    }
  }

  @media (max-width: 767px) {
    .nv-page {
      padding: var(--nv-space-4);
    }

    .nv-page--grid {
      grid-template-columns: minmax(0, 1fr);
    }
  }
}
</style>

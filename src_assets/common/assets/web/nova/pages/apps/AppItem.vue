<script setup>
/**
 * One application: a cover tile (grid) or a row (list: thumbnail, name, command, status
 * text, Edit and a "⋯" menu). Pressing the cover or the name opens the editor; below
 * 900px wide the list's Edit button moves into the menu.
 *
 * Props: app (required), index (host index; used for the element id so focus can return
 *        to it), coverUrl ('' shows the initial letter), layout ('grid' | 'list').
 * Emits: edit, delete, cover-error.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { Pencil, Trash2 } from '@lucide/vue'
import NvButton from '../../components/NvButton.vue'
import NvBadge from '../../components/NvBadge.vue'
import ActionMenu from './ActionMenu.vue'
import { appFlags } from './appForm'

const props = defineProps({
  app: { type: Object, required: true },
  index: { type: Number, required: true },
  coverUrl: { type: String, default: '' },
  layout: { type: String, default: 'grid' },
})
const emit = defineEmits(['edit', 'delete', 'cover-error'])
const { t } = useI18n()

const name = computed(() => props.app.name || t('nova.apps.unnamed'))
const initial = computed(() => (props.app.name || '?').charAt(0).toUpperCase())
const flags = computed(() => appFlags(props.app))
const command = computed(() => props.app.cmd || t('nova.apps.no_command'))
const menuItems = computed(() => [
  { id: 'edit', label: t('nova.apps.edit'), icon: Pencil },
  { id: 'delete', label: t('nova.apps.delete_ellipsis'), icon: Trash2, danger: true },
])

function onMenu(id) {
  emit(id === 'edit' ? 'edit' : 'delete')
}
</script>

<template>
  <li :class="['nv-app', `nv-app--${layout}`]">
    <button :id="`nv-app-${index}`" type="button" class="nv-app__open" :aria-label="t('nova.apps.edit_named', { name })"
            @click="emit('edit')">
      <span class="nv-app__cover">
        <img v-if="coverUrl" :src="coverUrl" alt="" loading="lazy" class="nv-app__img" @error="emit('cover-error')" />
        <span v-else class="nv-app__initial" aria-hidden="true">{{ initial }}</span>
      </span>
      <span class="nv-app__text">
        <span class="nv-app__name" :title="name">{{ name }}</span>
        <span v-if="layout === 'list'" class="nv-app__cmd" :title="app.cmd">{{ command }}</span>
      </span>
    </button>
    <div class="nv-app__meta">
      <ul v-if="flags.length" class="nv-app__flags" :aria-label="t('nova.apps.flags_label')">
        <li v-for="flag in flags" :key="flag"><NvBadge>{{ t(flag) }}</NvBadge></li>
      </ul>
      <NvButton v-if="layout === 'list'" size="sm" class="nv-app__edit" @click="emit('edit')">
        {{ t('nova.apps.edit') }}<span class="nv-visually-hidden"> {{ name }}</span>
      </NvButton>
      <ActionMenu :label="t('nova.apps.more_for', { name })" :items="menuItems" @select="onMenu" />
    </div>
  </li>
</template>

<style>
@layer components {
  .nv-app {
    display: flex;
    min-width: 0;
  }

  .nv-app__open {
    display: flex;
    min-width: 0;
    padding: 0;
    border: 0;
    background: transparent;
    color: var(--nv-text);
    font: inherit;
    text-align: left;
    cursor: pointer;
  }

  .nv-app__cover {
    display: flex;
    align-items: center;
    justify-content: center;
    flex-shrink: 0;
    overflow: hidden;
    border-radius: var(--nv-radius-md);
    border: 1px solid var(--nv-border);
    background: var(--nv-raised);
    color: var(--nv-text-secondary);
    font-weight: 600;
  }

  .nv-app__img {
    width: 100%;
    height: 100%;
    object-fit: cover;
  }

  .nv-app__text {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
    min-width: 0;
  }

  .nv-app__name {
    font-weight: 600;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-app__cmd {
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    color: var(--nv-text-secondary);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .nv-app__meta {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    min-width: 0;
  }

  .nv-app__flags {
    display: flex;
    flex-wrap: wrap;
    gap: var(--nv-space-1);
    min-width: 0;
    margin: 0;
    padding: 0;
    list-style: none;
  }

  /* Grid: cover tile for browsing art; name under it, status and menu below. */
  .nv-app--grid {
    flex-direction: column;
    gap: var(--nv-space-2);
  }

  .nv-app--grid .nv-app__open {
    flex-direction: column;
    gap: var(--nv-space-2);
  }

  .nv-app--grid .nv-app__cover {
    width: 100%;
    aspect-ratio: 3 / 4;
    font-size: 44px;
  }

  .nv-app--grid .nv-app__open:focus-visible {
    outline: none;
  }

  .nv-app--grid .nv-app__open:focus-visible .nv-app__cover {
    outline: var(--nv-focus-width) solid var(--nv-focus);
    outline-offset: 2px;
  }

  .nv-app--grid .nv-app__meta {
    align-items: flex-start;
  }

  .nv-app--grid .nv-menu {
    margin-left: auto;
  }

  /* List: rows of at least 56px. */
  .nv-app--list {
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 56px;
    padding: var(--nv-space-2) var(--nv-space-3);
  }

  .nv-app--list + .nv-app--list {
    border-top: 1px solid var(--nv-border);
  }

  .nv-app--list .nv-app__open {
    flex-grow: 1;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 44px;
    border-radius: var(--nv-radius-sm);
  }

  .nv-app--list .nv-app__cover {
    width: 36px;
    height: 48px;
    font-size: var(--nv-text-lg);
  }

  .nv-app--list .nv-app__meta {
    flex-shrink: 0;
  }

  @media (max-width: 899px) {
    .nv-app--list .nv-app__flags,
    .nv-app--list .nv-app__edit {
      display: none;
    }
  }
}
</style>

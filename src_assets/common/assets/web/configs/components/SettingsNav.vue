<script setup>
/**
 * Section list for the Settings page. Links change the URL hash, so a section can be
 * shared and Back returns to it. The current section is marked with aria-current.
 *
 * Props: sections ([{ id, title, count }] — count = search matches, shown while searching),
 *        active (id of the section in view), searching.
 */
import { useI18n } from 'vue-i18n'

defineProps({
  sections: { type: Array, required: true },
  active: { type: String, default: '' },
  searching: { type: Boolean, default: false },
})
const { t } = useI18n()
</script>

<template>
  <nav class="nv-settings-nav" :aria-label="t('nova.settings.sections_label')">
    <ul class="nv-settings-nav__list">
      <li v-for="section in sections" :key="section.id">
        <RouterLink :to="{ hash: `#${section.id}` }"
                    :class="['nv-settings-nav__link', { 'nv-settings-nav__link--active': active === section.id }]"
                    :aria-current="active === section.id ? 'location' : null">
          <span class="nv-settings-nav__title">{{ section.title }}</span>
          <span v-if="searching" class="nv-settings-nav__count">
            {{ section.count }}<span class="nv-visually-hidden"> {{ t('nova.settings.matches') }}</span>
          </span>
        </RouterLink>
      </li>
    </ul>
  </nav>
</template>

<style>
@layer components {
  .nv-settings-nav {
    position: sticky;
    top: var(--nv-space-6);
    align-self: flex-start;
    width: 196px;
    flex-shrink: 0;
  }

  .nv-settings-nav__list {
    display: flex;
    flex-direction: column;
    gap: 2px;
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-settings-nav__link {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    min-height: var(--nv-control-height-sm);
    padding: 0 10px;
    border-radius: var(--nv-radius-md);
    color: var(--nv-text-secondary);
    text-decoration: none;
  }

  .nv-settings-nav__link:hover {
    background: var(--nv-raised);
    color: var(--nv-text);
    text-decoration: none;
  }

  .nv-settings-nav__link--active {
    background: var(--nv-accent-tint);
    color: var(--nv-accent-text);
    font-weight: 500;
  }

  .nv-settings-nav__title {
    flex-grow: 1;
    min-width: 0;
  }

  .nv-settings-nav__count {
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    font-variant-numeric: tabular-nums;
    color: var(--nv-text-muted);
  }

  @media (max-width: 1023px) {
    .nv-settings-nav {
      position: sticky;
      top: 0;
      z-index: 5;
      width: auto;
      align-self: stretch;
      margin: 0 calc(-1 * var(--nv-space-4));
      padding: var(--nv-space-2) var(--nv-space-4);
      background: var(--nv-bg);
      border-bottom: 1px solid var(--nv-border);
    }

    .nv-settings-nav__list {
      flex-direction: row;
      gap: var(--nv-space-1);
      overflow-x: auto;
      scrollbar-width: thin;
    }

    .nv-settings-nav__link {
      white-space: nowrap;
    }
  }

  @media (max-width: 899px) {
    .nv-settings-nav {
      top: 56px;
    }
  }
}
</style>

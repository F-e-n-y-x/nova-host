<script setup>
/**
 * Minimal header for pages shown without the app shell (first-run setup, signed out).
 */
import NovaLogo from './nova/components/NovaLogo.vue'
import NvThemeSwitcher from './nova/components/NvThemeSwitcher.vue'
</script>

<template>
  <header class="nv-simple-header">
    <NovaLogo />
    <NvThemeSwitcher />
  </header>
</template>

<style>
@layer components {
  .nv-simple-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--nv-space-4);
    flex-wrap: wrap;
    padding: var(--nv-space-3) var(--nv-space-6);
    background: var(--nv-sidebar);
    border-bottom: 1px solid var(--nv-border);
  }
}
</style>

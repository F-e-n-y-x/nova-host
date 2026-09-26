<script setup>
/**
 * Centered single-card layout for pages shown outside the app shell (first-run setup,
 * signed out). Slots: default (card content). Props: title, intro.
 */
import { watchEffect } from 'vue'
import { useI18n } from 'vue-i18n'
import NovaLogo from '../../components/NovaLogo.vue'
import NvThemeSwitcher from '../../components/NvThemeSwitcher.vue'
import { project } from '../../project'

const props = defineProps({
  title: { type: String, required: true },
  intro: { type: String, default: '' },
})

const { t } = useI18n()

watchEffect(() => {
  if (typeof document !== 'undefined') document.title = `${props.title} · Nova`
})
</script>

<template>
  <div class="nv-auth">
    <main id="nv-main" class="nv-auth__main" tabindex="-1">
      <div class="nv-auth__brand"><NovaLogo :size="32" /></div>
      <div class="nv-auth__card">
        <h1 class="nv-auth__title">{{ title }}</h1>
        <p v-if="intro" class="nv-auth__intro">{{ intro }}</p>
        <slot />
      </div>
      <footer class="nv-auth__footer">
        <nav class="nv-auth__links" :aria-label="t('nova.help.resources')">
          <a :href="project.docsUrl" target="_blank" rel="noopener">{{ t('nova.help.docs') }}</a>
          <a :href="project.licenseUrl" target="_blank" rel="noopener">{{ t('nova.help.license') }}</a>
        </nav>
        <NvThemeSwitcher />
      </footer>
    </main>
  </div>
</template>

<style>
@layer components {
  .nv-auth {
    min-height: 100vh;
    min-height: 100dvh;
    display: flex;
    justify-content: center;
    background: var(--nv-bg);
    color: var(--nv-text);
  }

  .nv-auth__main {
    width: 100%;
    max-width: 480px;
    box-sizing: border-box;
    padding: var(--nv-space-10) var(--nv-space-4);
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-6);
    outline: none;
  }

  .nv-auth__brand {
    display: flex;
    justify-content: center;
  }

  .nv-auth__card {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-4);
    padding: var(--nv-space-8);
    border-radius: var(--nv-radius-lg);
    background: var(--nv-surface);
    border: 1px solid var(--nv-border);
  }

  .nv-auth__title {
    margin: 0;
    font-size: var(--nv-text-2xl);
    font-weight: 600;
    line-height: 1.25;
  }

  .nv-auth__intro {
    margin: 0;
    color: var(--nv-text-secondary);
  }

  .nv-auth__footer {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: space-between;
    gap: var(--nv-space-4);
  }

  .nv-auth__links {
    display: flex;
    gap: var(--nv-space-4);
    font-size: var(--nv-text-sm);
  }

  .nv-auth__links a {
    color: var(--nv-text-secondary);
  }

  @media (max-width: 480px) {
    .nv-auth__main {
      padding-top: var(--nv-space-6);
    }

    .nv-auth__card {
      padding: var(--nv-space-5);
    }
  }
}
</style>

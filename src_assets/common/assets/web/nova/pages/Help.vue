<script setup>
/**
 * Help: recommended streaming apps, project resources, legal documents and credits.
 */
import { useI18n } from 'vue-i18n'
import NvPage from '../components/NvPage.vue'
import NvCard from '../components/NvCard.vue'
import ResourceCard from '../../ResourceCard.vue'
import ClientList from './help/ClientList.vue'
import { upstreamProjects } from '../project'

const { t } = useI18n()
const [sunshine, zenith] = upstreamProjects
</script>

<template>
  <NvPage :title="t('nova.help.title')">
    <div class="nv-help">
      <ClientList />
      <ResourceCard />
      <NvCard :title="t('nova.help.credit_title')">
        <i18n-t keypath="nova.help.credit" tag="p" scope="global">
          <template #sunshine><a :href="sunshine.url" target="_blank" rel="noopener">{{ sunshine.name }}</a></template>
          <template #zenith><a :href="zenith.url" target="_blank" rel="noopener">{{ zenith.name }}</a></template>
        </i18n-t>
      </NvCard>
    </div>
  </NvPage>
</template>

<style>
@layer components {
  .nv-help {
    display: grid;
    grid-template-columns: repeat(2, minmax(0, 1fr));
    gap: var(--nv-space-5);
    align-items: start;
  }

  @media (max-width: 899px) {
    .nv-help {
      grid-template-columns: minmax(0, 1fr);
      gap: var(--nv-space-4);
    }
  }
}
</style>

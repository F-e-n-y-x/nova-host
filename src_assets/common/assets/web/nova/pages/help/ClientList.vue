<script setup>
/**
 * Recommended streaming apps for Nova. Static on purpose: no remote directory is fetched.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { ExternalLink } from '@lucide/vue'
import NvCard from '../../components/NvCard.vue'
import NvBadge from '../../components/NvBadge.vue'

const { t } = useI18n()

const CLIENTS = [
  { id: 'nebula', name: 'Nebula', url: '', soon: true },
  { id: 'moonlight', name: 'Moonlight', url: 'https://moonlight-stream.org', kind: 'website' },
  { id: 'artemis', name: 'Artemis', url: 'https://github.com/ClassicOldSong/moonlight-android', kind: 'github' },
  { id: 'voidlink', name: 'VoidLink', url: 'https://github.com/The-Fried-Fish/VoidLink-previously-moonlight-zwm', kind: 'github' },
]

const clients = computed(() => CLIENTS.map((c) => ({
  ...c,
  platforms: t(`nova.help.platforms_${c.id}`),
  description: t(`nova.help.client_${c.id}`),
  linkText: c.kind ? `${c.name} ${t(`nova.help.link_${c.kind}`)}` : '',
})))
</script>

<template>
  <NvCard id="clients" :title="t('nova.help.clients_title')" :span="2">
    <p class="nv-secondary nv-clients__intro">{{ t('nova.help.clients_intro') }}</p>
    <ul class="nv-clients">
      <li v-for="client in clients" :key="client.id" class="nv-clients__item">
        <div class="nv-clients__head">
          <p class="nv-clients__name">{{ client.name }}</p>
          <NvBadge v-if="client.soon" variant="accent">{{ t('nova.help.coming_soon') }}</NvBadge>
        </div>
        <p class="nv-clients__platforms">{{ client.platforms }}</p>
        <p class="nv-secondary nv-clients__desc">{{ client.description }}</p>
        <a v-if="client.url" class="nv-clients__link" :href="client.url" target="_blank" rel="noopener">
          {{ client.linkText }}<ExternalLink :size="14" aria-hidden="true" />
        </a>
      </li>
    </ul>
  </NvCard>
</template>

<style>
@layer components {
  .nv-clients__intro {
    margin: 0;
  }

  .nv-clients {
    list-style: none;
    margin: 0;
    padding: 0;
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
    gap: var(--nv-space-3);
  }

  .nv-clients__item {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
    padding: var(--nv-space-4);
    border-radius: var(--nv-radius-md);
    background: var(--nv-raised);
    min-width: 0;
  }

  .nv-clients__head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--nv-space-2);
  }

  .nv-clients__name {
    margin: 0;
    font-weight: 600;
  }

  .nv-clients__platforms {
    margin: 0;
    font-size: var(--nv-text-xs);
    color: var(--nv-text-muted);
  }

  .nv-clients__desc {
    margin: 0;
    font-size: var(--nv-text-sm);
    flex-grow: 1;
  }

  .nv-clients__link {
    display: inline-flex;
    align-items: center;
    gap: var(--nv-space-1);
    margin-top: var(--nv-space-2);
    min-height: 32px;
    font-size: var(--nv-text-sm);
    font-weight: 500;
    color: var(--nv-accent-text);
    overflow-wrap: anywhere;
  }
}
</style>

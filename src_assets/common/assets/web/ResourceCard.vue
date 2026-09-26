<script setup>
/**
 * Project resources and legal links (Help page and first-run setup).
 */
import { useI18n } from 'vue-i18n'
import { BookOpen, Bug, FileText, ScrollText } from '@lucide/vue'
import NvCard from './nova/components/NvCard.vue'
import NvButton from './nova/components/NvButton.vue'
import { project } from './nova/project'

const { t } = useI18n()
</script>

<template>
  <div class="nv-stack">
    <NvCard :title="t('nova.help.resources')">
      <div class="nv-row nv-resource-links">
        <NvButton variant="secondary" :href="project.docsUrl" target="_blank" rel="noopener">
          <BookOpen :size="18" aria-hidden="true" />{{ t('nova.help.docs') }}
        </NvButton>
        <NvButton variant="secondary" :href="project.issuesUrl" target="_blank" rel="noopener">
          <Bug :size="18" aria-hidden="true" />{{ t('nova.help.issues') }}
        </NvButton>
      </div>
    </NvCard>
    <NvCard :title="t('nova.help.legal')">
      <p class="nv-secondary">{{ t('nova.help.legal_desc') }}</p>
      <div class="nv-row nv-resource-links">
        <NvButton variant="secondary" :href="project.licenseUrl" target="_blank" rel="noopener">
          <FileText :size="18" aria-hidden="true" />{{ t('nova.help.license') }}
        </NvButton>
        <NvButton variant="secondary" :href="project.noticeUrl" target="_blank" rel="noopener">
          <ScrollText :size="18" aria-hidden="true" />{{ t('nova.help.notice') }}
        </NvButton>
      </div>
    </NvCard>
  </div>
</template>

<style>
@layer components {
  .nv-resource-links {
    flex-wrap: wrap;
  }
}
</style>

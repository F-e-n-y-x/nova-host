<script setup>
/**
 * Pair a device page (A+C design): the PIN card next to "How it works". Pairing requests are
 * polled every 2 seconds while the page is visible.
 */
import { onMounted, shallowRef } from 'vue'
import { useI18n } from 'vue-i18n'
import NvPage from './nova/components/NvPage.vue'
import PairSteps from './nova/pair/PairSteps.vue'
import PairForm from './nova/pair/PairForm.vue'
import { getConfig } from './nova/api'
import { usePendingPairings } from './nova/devices/usePendingPairings'

const { t } = useI18n()
const hostName = shallowRef('')
const { requests, loaded, failed, refresh } = usePendingPairings({ interval: 2000 })

onMounted(async () => {
  try {
    hostName.value = (await getConfig()).sunshine_name || ''
  } catch {
    hostName.value = ''
  }
})
</script>

<template>
  <NvPage :title="t('nova.pair.title')">
    <div class="nv-pair">
      <PairForm :requests="requests" :loaded="loaded" :failed="failed" :host-name="hostName"
                @changed="refresh" @retry="refresh" />
      <PairSteps :host-name="hostName" />
    </div>
  </NvPage>
</template>

<style scoped>
@layer components {
  .nv-pair {
    display: grid;
    grid-template-columns: minmax(0, 536px) minmax(0, 320px);
    gap: var(--nv-space-6);
    align-items: start;
    justify-content: center;
    padding-top: var(--nv-space-4);
  }

  @media (max-width: 1023px) {
    .nv-pair {
      grid-template-columns: minmax(0, 1fr);
      justify-content: stretch;
      padding-top: 0;
    }
  }
}
</style>

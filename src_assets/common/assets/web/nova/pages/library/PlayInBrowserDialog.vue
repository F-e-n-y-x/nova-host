<script setup>
/**
 * "Play in browser" dialog: pick Virtual display or Mirror desktop, then open the game in Nova's
 * browser client in a new tab. The link is also shown so it can be typed on another device, such
 * as a tablet without the app. When the browser client is off, it points to the Play in browser page.
 *
 * v-model:open — boolean.
 * Props: app (apps.json entry; required while open).
 */
import { computed, ref, watch } from 'vue'
import { useI18n } from 'vue-i18n'
import { ExternalLink } from '@lucide/vue'
import NvDialog from '../../components/NvDialog.vue'
import NvButton from '../../components/NvButton.vue'
import NvSegmentedControl from '../../components/NvSegmentedControl.vue'
import NvStatusDot from '../../components/NvStatusDot.vue'
import { defaultDisplayMode, getWebClient, playInBrowserUrl, probeWebClient } from '../../webClient'

const open = defineModel('open', { type: Boolean, default: false })
const props = defineProps({
  app: { type: Object, default: null },
})

const { t } = useI18n()
const mode = ref('virtual')
const reachable = ref(null) // null = checking
const status = ref(null)

const name = computed(() => props.app?.name || '')
const port = computed(() => status.value?.port)
const off = computed(() => status.value !== null && status.value.state !== 'running' && status.value.state !== 'starting')
const url = computed(() => playInBrowserUrl(name.value, mode.value, { port: port.value }))
const options = computed(() => [
  { value: 'virtual', label: t('nova.browser.virtual') },
  { value: 'mirror', label: t('nova.browser.mirror') },
])

watch(open, async (isOpen) => {
  if (!isOpen) return
  mode.value = defaultDisplayMode(props.app)
  reachable.value = null
  try {
    status.value = await getWebClient()
  } catch {
    status.value = null
  }
  if (off.value) return
  reachable.value = await probeWebClient({ port: port.value })
}, { immediate: true })

function launched() {
  open.value = false
}
</script>

<template>
  <NvDialog v-model:open="open" :title="t('nova.browser.title', { name })" :description="t('nova.browser.desc')" size="sm">
    <div class="nv-pib">
      <span id="nv-pib-display" class="nv-pib__label">{{ t('nova.browser.display') }}</span>
      <NvSegmentedControl v-model="mode" labelledby="nv-pib-display" :options="options" />
      <p class="nv-pib__hint">{{ mode === 'virtual' ? t('nova.browser.virtual_hint') : t('nova.browser.mirror_hint') }}</p>

      <div v-if="off" class="nv-pib__warn" role="status" data-nv-pib-off>
        <NvStatusDot status="neutral" :label="t('nova.browser.off')" />
        <p class="nv-pib__hint">{{ t('nova.browser.off_hint') }}</p>
      </div>
      <NvStatusDot v-else-if="reachable === null" status="neutral" :label="t('nova.browser.checking')" />
      <NvStatusDot v-else-if="reachable" status="success" :label="t('nova.browser.ready')" />
      <div v-else class="nv-pib__warn" role="status">
        <NvStatusDot status="warning" :label="t('nova.browser.unreachable')" />
        <p class="nv-pib__hint">{{ t('nova.browser.unreachable_hint') }}</p>
      </div>

      <span class="nv-pib__label">{{ t('nova.browser.link') }}</span>
      <code class="nv-pib__url" data-nv-play-url>{{ url }}</code>
    </div>
    <template #footer>
      <NvButton @click="open = false">{{ t('nova.common.cancel') }}</NvButton>
      <NvButton v-if="off" variant="primary" to="/browser" data-nv-pib-manage @click="launched">{{ t('nova.browser.manage') }}</NvButton>
      <NvButton v-else variant="primary" :href="url" :icon="ExternalLink" target="_blank" rel="noopener noreferrer" data-nv-play
                @click="launched">{{ t('nova.browser.open') }}</NvButton>
    </template>
  </NvDialog>
</template>

<style>
@layer components {
  .nv-pib {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: var(--nv-space-3);
  }

  .nv-pib__label {
    font-size: var(--nv-text-sm);
    font-weight: 500;
  }

  .nv-pib__hint {
    margin: 0;
    color: var(--nv-text-secondary);
    font-size: var(--nv-text-sm);
  }

  .nv-pib__warn {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
  }

  .nv-pib__url {
    max-width: 100%;
    padding: var(--nv-space-2) var(--nv-space-3);
    border: 1px solid var(--nv-divider);
    border-radius: var(--nv-radius-sm);
    font-size: var(--nv-text-xs);
    overflow-wrap: anywhere;
    user-select: all;
  }
}
</style>

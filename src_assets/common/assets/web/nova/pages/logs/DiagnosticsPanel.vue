<script setup>
/**
 * One-click fixes for common host problems. Every action is confirmed in a dialog first and
 * reports its result as a toast. Platform-specific actions appear only where they apply.
 *
 * Props: platform ('linux' | 'windows' | 'macos' | 'freebsd' | ''), loading (config still loading).
 */
import { computed, shallowRef } from 'vue'
import { useI18n } from 'vue-i18n'
import { MonitorCog, Power, RotateCcw, SquareX } from '@lucide/vue'
import NvCard from '../../components/NvCard.vue'
import NvButton from '../../components/NvButton.vue'
import NvDialog from '../../components/NvDialog.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import { postJson } from '../../api'
import { apiFetch } from '../../../fetch_utils'
import { toast } from '../../toast'

const props = defineProps({
  platform: { type: String, default: '' },
  loading: { type: Boolean, default: false },
})

const { t } = useI18n()

async function restartHost() {
  try {
    await apiFetch('./api/restart', { method: 'POST', headers: { 'Content-Type': 'application/json' } })
  } catch {
    // The connection drops while the host restarts; that is expected.
  }
  toast.info(t('nova.logs.restarting'), { timeout: 8000 })
}

const ACTIONS = [
  {
    id: 'close_app', icon: SquareX, platforms: null,
    async run() {
      const result = await postJson('./api/apps/close')
      if (result?.status) toast.success(t('nova.logs.close_app_done'))
      else toast.warning(t('nova.logs.close_app_none'))
    },
  },
  { id: 'restart', icon: Power, platforms: null, run: restartHost },
  {
    id: 'portal_reset', icon: RotateCcw, platforms: ['linux', 'freebsd'],
    async run() {
      const result = await postJson('./api/reset-portal-token')
      if (!result?.status) {
        toast.danger(t('nova.logs.portal_reset_failed'))
        return
      }
      toast.success(t('nova.logs.portal_reset_done'))
      await restartHost()
    },
  },
  {
    id: 'dd_reset', icon: MonitorCog, platforms: ['windows'],
    async run() {
      const result = await postJson('./api/reset-display-device-persistence')
      if (result?.status) toast.success(t('nova.logs.dd_reset_done'))
      else toast.danger(t('nova.logs.dd_reset_failed'))
    },
  },
]

const actions = computed(() => ACTIONS
  .filter((a) => !a.platforms || a.platforms.includes(props.platform))
  .map((a) => ({
    ...a,
    title: t(`nova.logs.${a.id}`),
    description: t(`nova.logs.${a.id}_desc`),
    button: t(`nova.logs.${a.id}_button`),
    confirmDescription: t(`nova.logs.${a.id}_confirm_desc`),
  })))

const pending = shallowRef(null)
const open = shallowRef(false)
const busy = shallowRef(false)

function ask(action) {
  pending.value = action
  open.value = true
}

async function confirm() {
  if (!pending.value) return
  busy.value = true
  try {
    await pending.value.run()
  } catch (error) {
    toast.danger(t('nova.logs.action_failed', { error: error.message }))
  } finally {
    busy.value = false
    open.value = false
  }
}
</script>

<template>
  <section class="nv-diag" aria-labelledby="nv-diag-title">
    <div class="nv-diag__head">
      <h2 id="nv-diag-title" class="nv-diag__title">{{ t('nova.logs.diagnostics') }}</h2>
      <p class="nv-secondary nv-diag__desc">{{ t('nova.logs.diagnostics_desc') }}</p>
    </div>
    <div class="nv-diag__grid">
      <NvCard v-for="action in actions" :key="action.id" :title="action.title" :level="3" class="nv-diag__card">
        <p class="nv-secondary nv-diag__desc">{{ action.description }}</p>
        <template #footer>
          <NvButton variant="danger" @click="ask(action)">
            <component :is="action.icon" :size="16" aria-hidden="true" />{{ action.button }}
          </NvButton>
        </template>
      </NvCard>
      <NvCard v-if="loading" :level="3" class="nv-diag__card" aria-busy="true">
        <NvSkeleton :lines="3" />
      </NvCard>
    </div>

    <NvDialog v-model:open="open" :title="pending?.title || ''" :description="pending?.confirmDescription || ''" :persistent="busy">
      <template #footer>
        <NvButton variant="secondary" :disabled="busy" @click="open = false">{{ t('nova.common.cancel') }}</NvButton>
        <NvButton variant="danger-solid" :loading="busy" @click="confirm">{{ pending?.button || t('nova.logs.confirm') }}</NvButton>
      </template>
    </NvDialog>
  </section>
</template>

<style>
@layer components {
  .nv-diag {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-3);
  }

  .nv-diag__head {
    display: flex;
    flex-wrap: wrap;
    align-items: baseline;
    gap: var(--nv-space-1) var(--nv-space-4);
  }

  .nv-diag__title {
    margin: 0;
    font-size: var(--nv-text-lg);
    font-weight: 600;
  }

  .nv-diag__desc {
    margin: 0;
  }

  .nv-diag__grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(260px, 1fr));
    gap: var(--nv-space-4);
  }

  .nv-diag__card {
    padding: var(--nv-space-5);
  }
}
</style>

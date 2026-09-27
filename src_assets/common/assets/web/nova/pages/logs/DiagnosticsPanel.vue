<script setup>
/**
 * One-click fixes for common host problems. Every action is confirmed in a dialog first and
 * reports its result as a toast. Platform-specific actions appear only where they apply.
 *
 * Props: platform ('linux' | 'windows' | 'macos' | 'freebsd' | ''), loading (config still loading).
 */
import { computed, nextTick, shallowRef, useId } from 'vue'
import { useI18n } from 'vue-i18n'
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
    id: 'close_app', platforms: null,
    async run() {
      const result = await postJson('./api/apps/close')
      if (result?.status) toast.success(t('nova.logs.close_app_done'))
      else toast.warning(t('nova.logs.close_app_none'))
    },
  },
  { id: 'restart', platforms: null, run: restartHost },
  {
    id: 'portal_reset', platforms: ['linux', 'freebsd'],
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
    id: 'dd_reset', platforms: ['windows'],
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
    confirmTitle: t(`nova.logs.${a.id}_confirm_title`),
    confirmDescription: t(`nova.logs.${a.id}_confirm_desc`),
  })))

const pending = shallowRef(null)
const open = shallowRef(false)
const busy = shallowRef(false)
const cancelId = `nv-diag-cancel-${useId()}`

async function ask(action) {
  pending.value = action
  open.value = true
  // Start on the safe choice; runs after the dialog's own initial focus.
  await nextTick()
  setTimeout(() => document.getElementById(cancelId)?.focus(), 0)
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
  <NvCard :title="t('nova.logs.actions_title')" class="nv-diag" flush>
    <ul class="nv-diag__list">
      <li v-for="action in actions" :key="action.id" class="nv-diag__row">
        <div class="nv-diag__text">
          <span class="nv-diag__name">{{ action.title }}</span>
          <span class="nv-diag__desc">{{ action.description }}</span>
        </div>
        <NvButton size="sm" :variant="action.id === 'restart' ? 'danger' : 'secondary'" @click="ask(action)">
          {{ action.button }}
        </NvButton>
      </li>
      <li v-if="loading" class="nv-diag__row" aria-busy="true">
        <span class="nv-visually-hidden">{{ t('nova.common.loading') }}</span>
        <NvSkeleton :lines="2" />
      </li>
    </ul>

    <NvDialog v-model:open="open" :title="pending?.confirmTitle || ''" :description="pending?.confirmDescription || ''" :persistent="busy">
      <template #footer>
        <NvButton :id="cancelId" variant="secondary" :disabled="busy" @click="open = false">{{ t('nova.common.cancel') }}</NvButton>
        <NvButton variant="danger-solid" :loading="busy" @click="confirm">{{ pending?.button || t('nova.logs.confirm') }}</NvButton>
      </template>
    </NvDialog>
  </NvCard>
</template>

<style>
@layer components {
  .nv-diag__list {
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-diag__row {
    display: flex;
    align-items: center;
    gap: var(--nv-space-4);
    min-height: 60px;
    padding: var(--nv-space-3) var(--nv-space-5);
    border-top: 1px solid var(--nv-divider);
  }

  .nv-diag__row:first-child {
    border-top: 0;
  }

  .nv-diag__text {
    display: flex;
    flex-direction: column;
    flex-grow: 1;
    min-width: 0;
    line-height: 1.35;
  }

  .nv-diag__name {
    font-weight: 500;
  }

  .nv-diag__desc {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }
}
</style>

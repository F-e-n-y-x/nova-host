<script setup>
/**
 * "Checks" card: the host's own health checks (GET /api/health) — encoder, capture,
 * clipboard, input, audio, display, exposure. Problems show what's wrong and how to fix
 * it: a command to copy or a link to the setting. Hidden when the host has no health API.
 *
 * Props: load (optional loader for tests; resolves to the /api/health JSON or null).
 */
import { computed, onMounted, shallowRef } from 'vue'
import { useI18n } from 'vue-i18n'
import { Copy } from '@lucide/vue'
import NvCard from '../../components/NvCard.vue'
import NvIconButton from '../../components/NvIconButton.vue'
import NvSkeleton from '../../components/NvSkeleton.vue'
import { toast } from '../../toast'
import { apiFetch } from '../../../fetch_utils'

const props = defineProps({
  load: { type: Function, default: undefined },
})
const { t } = useI18n()

const checks = shallowRef(null)
const loading = shallowRef(true)
const available = shallowRef(true)

async function defaultLoad() {
  const response = await apiFetch('./api/health')
  if (!response.ok) return null
  return response.json()
}

async function refresh() {
  loading.value = true
  try {
    const data = await (props.load || defaultLoad)()
    available.value = Array.isArray(data?.checks)
    checks.value = available.value ? data.checks : null
  } catch {
    available.value = false
  } finally {
    loading.value = false
  }
}
onMounted(refresh)

const ORDER = { error: 0, warn: 1, ok: 2 }
const rows = computed(() => (checks.value || []).slice()
  .sort((a, b) => (ORDER[a.status] ?? 3) - (ORDER[b.status] ?? 3))
  .map((c) => ({
    ...c,
    statusLabel: t(`nova.logs.check_${c.status === 'ok' ? 'ok' : c.status === 'error' ? 'error' : 'warn'}`),
    command: c.fix?.kind === 'command' ? c.fix.value : '',
    setting: c.fix?.kind === 'setting' ? c.fix.value : '',
    doc: c.fix?.kind === 'doc' ? c.fix.value : '',
  })))
const problems = computed(() => rows.value.filter((r) => r.status !== 'ok').length)

async function copy(command) {
  try {
    await navigator.clipboard.writeText(command)
    toast.success(t('nova.logs.check_copied'))
  } catch {
    toast.warning(t('nova.logs.copy_failed'))
  }
}
</script>

<template>
  <NvCard v-if="loading || available" :title="t('nova.logs.checks_title')" class="nv-checks" flush>
    <template v-if="!loading" #actions>
      <span class="nv-secondary nv-checks__summary">
        {{ problems ? t('nova.logs.checks_problems', { n: problems }, problems) : t('nova.logs.checks_all_ok') }}
      </span>
    </template>
    <div v-if="loading" aria-busy="true" class="nv-checks__loading">
      <span class="nv-visually-hidden">{{ t('nova.common.loading') }}</span>
      <NvSkeleton :lines="5" />
    </div>
    <ul v-else class="nv-checks__list">
      <li v-for="row in rows" :key="row.id" :class="['nv-checks__row', `nv-checks__row--${row.status}`]">
        <div class="nv-checks__line">
          <span class="nv-checks__dot" aria-hidden="true"></span>
          <span class="nv-checks__title">{{ row.title }}</span>
          <span v-if="row.status === 'ok'" class="nv-checks__status">{{ row.statusLabel }}</span>
          <span v-else class="nv-visually-hidden">{{ row.statusLabel }}</span>
        </div>
        <template v-if="row.status !== 'ok'">
          <p class="nv-checks__detail">{{ row.detail }}</p>
          <div v-if="row.command" class="nv-checks__fix">
            <code class="nv-checks__code">{{ row.command }}</code>
            <NvIconButton size="sm" variant="ghost" :label="t('nova.logs.check_copy', { command: row.command })"
                          @click="copy(row.command)">
              <Copy :size="14" aria-hidden="true" />
            </NvIconButton>
          </div>
          <RouterLink v-else-if="row.setting" class="nv-checks__link" :to="`/settings#${row.setting}`">
            {{ t('nova.logs.check_open_setting') }}
          </RouterLink>
          <a v-else-if="row.doc" class="nv-checks__link" :href="row.doc" target="_blank" rel="noopener noreferrer">
            {{ t('nova.logs.check_read_more') }}
          </a>
        </template>
      </li>
    </ul>
  </NvCard>
</template>

<style>
@layer components {
  .nv-checks__summary {
    font-size: var(--nv-text-sm);
  }

  .nv-checks__loading {
    padding: var(--nv-space-4) var(--nv-space-5);
  }

  .nv-checks__list {
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .nv-checks__row {
    display: flex;
    flex-direction: column;
    gap: var(--nv-space-1);
    padding: var(--nv-space-3) var(--nv-space-5);
    border-top: 1px solid var(--nv-divider);
  }

  .nv-checks__row:first-child {
    border-top: 0;
  }

  .nv-checks__line {
    display: flex;
    align-items: center;
    gap: var(--nv-space-3);
    min-height: 24px;
  }

  .nv-checks__dot {
    width: 8px;
    height: 8px;
    border-radius: var(--nv-radius-pill);
    background: var(--nv-success);
    flex-shrink: 0;
  }

  .nv-checks__row--warn .nv-checks__dot {
    background: var(--nv-warning);
  }

  .nv-checks__row--error .nv-checks__dot {
    background: var(--nv-danger);
  }

  .nv-checks__title {
    flex-grow: 1;
    min-width: 0;
    font-weight: 500;
  }

  .nv-checks__status {
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-checks__row--warn .nv-checks__title {
    color: var(--nv-warning);
  }

  .nv-checks__row--error .nv-checks__title {
    color: var(--nv-danger);
  }

  .nv-checks__detail {
    margin: 0 0 0 20px;
    font-size: var(--nv-text-sm);
    color: var(--nv-text-secondary);
  }

  .nv-checks__fix {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    margin-left: 20px;
  }

  .nv-checks__code {
    min-width: 0;
    padding: 2px var(--nv-space-2);
    border-radius: var(--nv-radius-sm);
    background: var(--nv-raised);
    border: 1px solid var(--nv-border);
    font-family: var(--nv-font-mono);
    font-size: var(--nv-text-xs);
    overflow-wrap: anywhere;
  }

  .nv-checks__link {
    margin-left: 20px;
    font-size: var(--nv-text-sm);
    font-weight: 500;
  }
}
</style>

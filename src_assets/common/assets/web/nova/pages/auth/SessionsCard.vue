<script setup>
/**
 * Signed-in browsers (GET /api/auth/sessions), each with a Sign out button; "Sign out all
 * others" keeps this browser. Signing this browser out goes to the sign-in page.
 */
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import NvCard from '../../components/NvCard.vue'
import NvButton from '../../components/NvButton.vue'
import NvAlert from '../../components/NvAlert.vue'
import NvBadge from '../../components/NvBadge.vue'
import { fetchJson, postJson, useAsync } from '../../api'
import { signOut } from '../../auth'
import { relativeTime } from '../../devices/format'
import { toast } from '../../toast'

const { t, locale } = useI18n()
const { data, error, loading, reload } = useAsync(() => fetchJson('./api/auth/sessions'))
const sessions = computed(() => data.value?.sessions || [])
const others = computed(() => sessions.value.filter((s) => !s.current))

/**
 * "Firefox on Linux" from a User-Agent header; the raw string is too long to be useful.
 *
 * @param {string} ua User-Agent.
 * @returns {string} Short description, or '' when nothing is recognised.
 */
function describe(ua = '') {
  const browser = [
    [/Edg\//, 'Edge'], [/OPR\//, 'Opera'], [/Firefox\//, 'Firefox'], [/Chrome\//, 'Chrome'], [/Safari\//, 'Safari'], [/curl\//, 'curl'],
  ].find(([re]) => re.test(ua))?.[1]
  const os = [
    [/Android/, 'Android'], [/iPhone|iPad/, 'iOS'], [/Windows/, 'Windows'], [/Mac OS X|Macintosh/, 'macOS'], [/CrOS/, 'ChromeOS'], [/Linux/, 'Linux'],
  ].find(([re]) => re.test(ua))?.[1]
  if (browser && os) return `${browser} · ${os}`
  return browser || os || ''
}

const nameOf = (s) => describe(s.user_agent) || t('nova.auth.sessions_unknown')

async function revoke(session) {
  if (session.current) {
    await signOut()
    return
  }
  try {
    await postJson('./api/auth/sessions/revoke', { id: session.id })
    toast.success(t('nova.auth.sessions_revoked'))
  } finally {
    reload()
  }
}

async function revokeOthers() {
  try {
    await postJson('./api/auth/sessions/revoke', { others: true })
    toast.success(t('nova.auth.sessions_revoked'))
  } finally {
    reload()
  }
}
</script>

<template>
  <NvCard :title="t('nova.auth.sessions_title')" class="nv-sessions">
    <template v-if="others.length" #actions>
      <NvButton size="sm" variant="ghost" @click="revokeOthers">{{ t('nova.auth.sessions_revoke_others') }}</NvButton>
    </template>
    <p class="nv-secondary nv-sessions__intro">{{ t('nova.auth.sessions_intro') }}</p>
    <NvAlert v-if="error" variant="danger">{{ t('nova.auth.sessions_failed') }}</NvAlert>
    <ul v-else-if="!loading || sessions.length" class="nv-sessions__list">
      <li v-for="s in sessions" :key="s.id" class="nv-sessions__row">
        <div class="nv-sessions__main">
          <p class="nv-sessions__name">
            {{ nameOf(s) }}
            <NvBadge v-if="s.current" variant="accent">{{ t('nova.auth.sessions_this') }}</NvBadge>
          </p>
          <p class="nv-sessions__meta nv-secondary">
            <span class="nv-mono">{{ s.address }}</span>
            · {{ t('nova.auth.sessions_last_seen', { when: relativeTime(s.last_seen, locale) }) }}
            · {{ s.remember ? t('nova.auth.sessions_kept') : t('nova.auth.sessions_browser_session') }}
          </p>
        </div>
        <NvButton size="sm" :aria-label="t('nova.auth.sessions_revoke_label', { name: s.current ? t('nova.auth.sessions_this') : nameOf(s) })"
                  @click="revoke(s)">{{ t('nova.auth.sessions_revoke') }}</NvButton>
      </li>
      <li v-if="!others.length" class="nv-sessions__empty nv-secondary">{{ t('nova.auth.sessions_none') }}</li>
    </ul>
  </NvCard>
</template>

<style>
@layer components {
  .nv-sessions {
    max-width: 560px;
  }

  .nv-sessions__intro {
    margin: 0 0 var(--nv-space-3);
    font-size: var(--nv-text-sm);
  }

  .nv-sessions__list {
    list-style: none;
    margin: 0;
    padding: 0;
  }

  .nv-sessions__row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--nv-space-4);
    padding: var(--nv-space-3) 0;
    border-top: 1px solid var(--nv-border);
  }

  .nv-sessions__main {
    min-width: 0;
  }

  .nv-sessions__name {
    display: flex;
    align-items: center;
    gap: var(--nv-space-2);
    margin: 0;
    font-weight: 500;
  }

  .nv-sessions__meta {
    margin: 2px 0 0;
    font-size: var(--nv-text-sm);
    overflow-wrap: anywhere;
  }

  .nv-sessions__empty {
    padding: var(--nv-space-3) 0 0;
    border-top: 1px solid var(--nv-border);
    font-size: var(--nv-text-sm);
  }
}
</style>

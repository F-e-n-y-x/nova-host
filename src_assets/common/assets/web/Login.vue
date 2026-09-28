<script setup>
/**
 * Sign-in page (/login). The host sets an HttpOnly session cookie; afterwards the page goes to
 * `?next=` (a same-origin path only) or the overview. A 429 from the host shows a countdown.
 */
import { computed, onBeforeUnmount, onMounted, reactive, shallowRef, useTemplateRef } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRoute, useRouter } from 'vue-router'
import NvTextField from './nova/components/NvTextField.vue'
import NvCheckbox from './nova/components/NvCheckbox.vue'
import NvButton from './nova/components/NvButton.vue'
import NvAlert from './nova/components/NvAlert.vue'
import AuthLayout from './nova/pages/auth/AuthLayout.vue'
import { getSession, nav, safeNext, signIn } from './nova/auth'

const { t } = useI18n()
const route = useRoute()
const router = useRouter()
const formEl = useTemplateRef('formEl')
const usernameField = useTemplateRef('usernameField')
const passwordField = useTemplateRef('passwordField')

const values = reactive({ username: '', password: '', remember: false })
const submitted = shallowRef(false)
const busy = shallowRef(false)
/** '' | 'wrong' | 'locked' | 'error' */
const failure = shallowRef('')
const failureDetail = shallowRef('')
const lockedFor = shallowRef(0)
const lockedTotal = shallowRef(0)
let timer = null

const destination = computed(() => safeNext(route.query.next) || '/')
const signedOut = computed(() => route.query.signed_out === '1' && !failure.value)
const usernameError = computed(() => (submitted.value && !values.username.trim() ? t('nova.auth.username_required') : ''))
const passwordError = computed(() => (submitted.value && !values.password ? t('nova.auth.password_required') : ''))
const locked = computed(() => lockedFor.value > 0)

/** "0:27" or "4:05". */
function clock(seconds) {
  const s = Math.max(0, Math.ceil(seconds))
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`
}

function stopTimer() {
  if (timer) clearInterval(timer)
  timer = null
}

function startLockout(seconds) {
  stopTimer()
  lockedTotal.value = seconds
  lockedFor.value = seconds
  const until = Date.now() + seconds * 1000
  timer = setInterval(() => {
    lockedFor.value = Math.max(0, Math.ceil((until - Date.now()) / 1000))
    if (lockedFor.value === 0) {
      stopTimer()
      failure.value = ''
      passwordField.value?.focus()
    }
  }, 250)
}

function go() {
  nav.replace(destination.value)
}

async function submit() {
  submitted.value = true
  if (locked.value || busy.value) return
  if (!values.username.trim() || !values.password) {
    ;(values.username.trim() ? passwordField : usernameField).value?.focus()
    return
  }
  busy.value = true
  failure.value = ''
  try {
    const result = await signIn({ username: values.username.trim(), password: values.password, remember: values.remember })
    if (result.ok) {
      go()
      return
    }
    if (result.reason === 'setup') {
      router.replace('/welcome')
      return
    }
    failure.value = result.reason
    failureDetail.value = result.message || ''
    if (result.reason === 'locked') startLockout(result.retryAfter)
    values.password = ''
    submitted.value = false
    passwordField.value?.focus()
  } finally {
    busy.value = false
  }
}

onMounted(async () => {
  try {
    await router.isReady()
    const session = await getSession()
    if (session?.setup_required) {
      router.replace('/welcome')
    } else if (session?.authenticated) {
      go()
    }
  } catch {
    // The host didn't answer; the form still works once it does.
  }
})

onBeforeUnmount(stopTimer)
</script>

<template>
  <AuthLayout :title="t('nova.auth.login_title')" :intro="t('nova.auth.login_intro')">
    <NvAlert v-if="signedOut" variant="success" live>{{ t('nova.auth.signed_out_notice') }}</NvAlert>
    <NvAlert v-if="failure === 'wrong'" variant="danger" live>{{ t('nova.auth.login_wrong') }}</NvAlert>
    <NvAlert v-else-if="failure === 'locked'" variant="warning" live :title="t('nova.auth.login_locked_title')">
      {{ t('nova.auth.login_locked', { time: clock(lockedTotal) }) }}
    </NvAlert>
    <NvAlert v-else-if="failure === 'error'" variant="danger" live>
      {{ t('nova.auth.login_error', { error: failureDetail || t('nova.auth.server_error') }) }}
    </NvAlert>

    <form ref="formEl" class="nv-auth-form" method="post" novalidate @submit.prevent="submit">
      <NvTextField ref="usernameField" v-model="values.username" name="username" :label="t('nova.auth.username')"
                   autocomplete="username" autocapitalize="none" spellcheck="false" required autofocus
                   :error="usernameError" />
      <NvTextField ref="passwordField" v-model="values.password" name="password" type="password"
                   :label="t('nova.auth.password')" autocomplete="current-password" required :error="passwordError" />
      <NvCheckbox v-model="values.remember" :label="t('nova.auth.remember')" :description="t('nova.auth.remember_desc')" />
      <NvButton type="submit" variant="primary" size="lg" block :loading="busy" :disabled="locked">
        <template v-if="locked">
          <span aria-hidden="true">{{ t('nova.auth.login_wait', { time: clock(lockedFor) }) }}</span>
          <span class="nv-visually-hidden">{{ t('nova.auth.login_wait_sr') }}</span>
        </template>
        <template v-else>{{ t('nova.auth.login_submit') }}</template>
      </NvButton>
    </form>
  </AuthLayout>
</template>

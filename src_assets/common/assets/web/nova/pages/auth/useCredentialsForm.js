/**
 * @file Shared state for the create-account (Welcome) and change-password forms: values,
 * inline validation that appears after a field is left or the form is submitted, and
 * submission to /api/password.
 */
import { computed, reactive, shallowRef } from 'vue'
import { apiFetch } from '../../../fetch_utils'

/** Minimum length for a new web UI password. */
export const MIN_PASSWORD_LENGTH = 8

/** serverError value when the host failed without saying why. */
export const GENERIC_ERROR = 'generic'

/** Milliseconds before reloading after a successful save, so the browser asks for the new credentials. */
export const RELOAD_DELAY_MS = 4000

/**
 * Validation errors for credentials, as i18n keys (params in `{key, params}` objects).
 *
 * @param {{username: string, password: string, confirm: string}} values Field values.
 * @param {{usernameRequired: boolean}} rules Whether the username may be empty.
 * @returns {{username?: object, password?: object, confirm?: object}} Errors by field.
 */
export function validateCredentials(values, { usernameRequired }) {
  const errors = {}
  if (usernameRequired && !values.username.trim()) errors.username = { key: 'nova.auth.username_required' }
  if (values.password.length < MIN_PASSWORD_LENGTH) {
    errors.password = { key: 'nova.auth.password_short', params: { min: MIN_PASSWORD_LENGTH } }
  }
  if (values.confirm !== values.password) errors.confirm = { key: 'nova.auth.password_mismatch' }
  return errors
}

/**
 * Error text from a failed /api/password response.
 *
 * @param {Response} response The response.
 * @returns {Promise<string>} The host's error message, or '' when it gave none.
 */
async function responseError(response) {
  try {
    const body = await response.json()
    return body?.error || ''
  } catch {
    return ''
  }
}

/**
 * Credentials form state.
 *
 * @param {object} options Options.
 * @param {boolean} options.usernameRequired Require the (new) username.
 * @param {(values: object) => object} options.toBody Build the /api/password body.
 * @param {() => void} [options.onSaved] Called after a successful save (default: reload later).
 * @returns {object} values, errors, touch(), submit(), saving, saved, serverError.
 */
export function useCredentialsForm({ usernameRequired, toBody, onSaved }) {
  const values = reactive({ username: '', password: '', confirm: '', currentUsername: '', currentPassword: '' })
  const touched = reactive({ username: false, password: false, confirm: false })
  const submitted = shallowRef(false)
  const saving = shallowRef(false)
  const saved = shallowRef(false)
  const serverError = shallowRef('')

  const allErrors = computed(() => validateCredentials(values, { usernameRequired }))
  const errors = computed(() => {
    const shown = {}
    for (const [field, error] of Object.entries(allErrors.value)) {
      if (submitted.value || touched[field]) shown[field] = error
    }
    return shown
  })

  function touch(field) {
    touched[field] = true
  }

  async function submit() {
    submitted.value = true
    serverError.value = ''
    if (Object.keys(allErrors.value).length > 0) return false
    saving.value = true
    try {
      const response = await apiFetch('./api/password', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(toBody(values)),
      })
      if (!response.ok) {
        serverError.value = (await responseError(response)) || GENERIC_ERROR
        return false
      }
      const body = await response.json()
      if (body?.status !== true) {
        serverError.value = body?.error || GENERIC_ERROR
        return false
      }
      saved.value = true
      ;(onSaved || scheduleReload)()
      return true
    } catch (error) {
      serverError.value = error.message || GENERIC_ERROR
      return false
    } finally {
      saving.value = false
    }
  }

  return { values, errors, touch, submit, saving, saved, serverError }
}

function scheduleReload() {
  setTimeout(() => globalThis.location.reload(), RELOAD_DELAY_MS)
}

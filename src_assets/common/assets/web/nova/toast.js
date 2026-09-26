/**
 * @file Toast queue shared by NvToastHost (mounted once in the app shell) and any page.
 *
 * Usage: `import { toast } from '../toast'` then `toast.success(t('…'))`.
 */
import { reactive } from 'vue'

export const toasts = reactive([])

let nextId = 1

/**
 * Show a toast.
 *
 * @param {string} message Text to show (already translated).
 * @param {object} [options] Options.
 * @param {'info'|'success'|'warning'|'danger'} [options.variant] Visual style.
 * @param {number} [options.timeout] Milliseconds before it hides; 0 keeps it until dismissed.
 * @returns {number} The toast id.
 */
export function showToast(message, { variant = 'info', timeout = 5000 } = {}) {
  const id = nextId++
  toasts.push({ id, message, variant })
  if (timeout > 0) {
    setTimeout(() => dismissToast(id), timeout)
  }
  return id
}

/**
 * Hide a toast.
 *
 * @param {number} id Toast id returned by showToast.
 */
export function dismissToast(id) {
  const index = toasts.findIndex((t) => t.id === id)
  if (index !== -1) toasts.splice(index, 1)
}

export const toast = {
  info: (message, options) => showToast(message, { ...options, variant: 'info' }),
  success: (message, options) => showToast(message, { ...options, variant: 'success' }),
  warning: (message, options) => showToast(message, { timeout: 8000, ...options, variant: 'warning' }),
  danger: (message, options) => showToast(message, { timeout: 0, ...options, variant: 'danger' }),
}

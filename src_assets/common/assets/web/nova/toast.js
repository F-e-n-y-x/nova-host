/**
 * @file Toast queue shared by NvToastHost (mounted once in the app shell) and any page.
 *
 * Usage: `import { toast } from '../toast'` then `toast.success(t('…'))`, or with an action:
 * `toast.info(t('nova.apps.deleted', { name }), { action: { label: t('nova.common.undo'), onClick: restore } })`.
 * Info/success hide after 5 s (paused while hovered or focused); warning/danger stay until dismissed.
 */
import { reactive } from 'vue'

export const toasts = reactive([])

let nextId = 1
const timers = new Map()

function startTimer(item) {
  if (!(item.remaining > 0)) return
  item.startedAt = Date.now()
  timers.set(item.id, setTimeout(() => dismissToast(item.id), item.remaining))
}

/**
 * Show a toast.
 *
 * @param {string} message Text to show (already translated).
 * @param {object} [options] Options.
 * @param {'info'|'success'|'warning'|'danger'} [options.variant] Visual style.
 * @param {number} [options.timeout] Milliseconds before it hides; 0 keeps it until dismissed.
 * @param {{label: string, onClick: Function}} [options.action] One action button (e.g. Undo);
 *   clicking it runs onClick and dismisses the toast.
 * @returns {number} The toast id.
 */
export function showToast(message, { variant = 'info', timeout = 5000, action = null } = {}) {
  const id = nextId++
  toasts.push({ id, message, variant, action, remaining: timeout, startedAt: 0 })
  startTimer(toasts[toasts.length - 1])
  return id
}

/**
 * Hide a toast.
 *
 * @param {number} id Toast id returned by showToast.
 */
export function dismissToast(id) {
  clearTimeout(timers.get(id))
  timers.delete(id)
  const index = toasts.findIndex((t) => t.id === id)
  if (index !== -1) toasts.splice(index, 1)
}

/**
 * Pause a toast's auto-hide timer (hover or keyboard focus inside it).
 *
 * @param {number} id Toast id.
 */
export function pauseToast(id) {
  const item = toasts.find((t) => t.id === id)
  if (!item || !timers.has(id)) return
  clearTimeout(timers.get(id))
  timers.delete(id)
  item.remaining = Math.max(1000, item.remaining - (Date.now() - item.startedAt))
}

/**
 * Resume a paused toast's auto-hide timer.
 *
 * @param {number} id Toast id.
 */
export function resumeToast(id) {
  const item = toasts.find((t) => t.id === id)
  if (item && !timers.has(id)) startTimer(item)
}

/**
 * Run a toast's action and dismiss it.
 *
 * @param {number} id Toast id.
 */
export function runToastAction(id) {
  const item = toasts.find((t) => t.id === id)
  const handler = item?.action?.onClick
  dismissToast(id)
  if (typeof handler === 'function') handler()
}

export const toast = {
  info: (message, options) => showToast(message, { ...options, variant: 'info' }),
  success: (message, options) => showToast(message, { ...options, variant: 'success' }),
  warning: (message, options) => showToast(message, { timeout: 0, ...options, variant: 'warning' }),
  danger: (message, options) => showToast(message, { timeout: 0, ...options, variant: 'danger' }),
}

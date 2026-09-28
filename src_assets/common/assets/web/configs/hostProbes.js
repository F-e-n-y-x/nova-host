/**
 * @file What the host can tell the Settings page about itself: displays it can capture,
 * audio sinks, and the encoder it detected. Each probe is fetched once per page load and
 * is `null` when the host doesn't offer that endpoint (older builds answer 404), so the
 * page falls back to free-text fields.
 */
import { shallowRef } from 'vue'
import { apiFetch } from '../fetch_utils.js'

const displays = shallowRef(null)
const sinks = shallowRef(null)
const hostInfo = shallowRef(null)
let started = null

async function getOptional(url) {
  try {
    const response = await apiFetch(url)
    if (!response.ok) return null
    return await response.json()
  } catch {
    return null
  }
}

/** Start the probes (once) and return their refs. */
export function useHostProbes() {
  if (!started) {
    started = Promise.all([
      getOptional('./api/displays').then((r) => { displays.value = Array.isArray(r?.displays) ? r.displays : null }),
      getOptional('./api/audio/sinks').then((r) => { sinks.value = Array.isArray(r?.sinks) ? r.sinks : null }),
      getOptional('./api/host/info').then((r) => { hostInfo.value = r && typeof r === 'object' ? r : null }),
    ])
  }
  return { displays, sinks, hostInfo, ready: started }
}

/** Forget cached probes (tests). */
export function resetHostProbes() {
  started = null
  displays.value = null
  sinks.value = null
  hostInfo.value = null
}

/**
 * Choices for a picker option, or null when the host didn't provide a list.
 *
 * @param {'displays'|'sinks'} source Probe to read.
 * @param {string} current Current value, kept as a choice even if the host no longer lists it.
 * @param {(key: string, params?: object) => string} t Translate function.
 * @returns {{value: string, label: string}[] | null}
 */
export function pickerChoices(source, current, t) {
  let list = null
  if (source === 'displays' && displays.value) {
    list = displays.value.filter((d) => d.connected !== false).map((d) => {
      const size = d.width && d.height ? `${d.width}×${d.height}` : ''
      const hz = d.refresh_hz ? `${Math.round(d.refresh_hz)} Hz` : ''
      const extra = [size, hz, d.primary ? t('nova.settings.picker_primary') : ''].filter(Boolean).join(' · ')
      return { value: String(d.name), label: extra ? `${d.name} — ${extra}` : String(d.name) }
    })
  } else if (source === 'sinks' && sinks.value) {
    list = sinks.value.map((s) => {
      const name = s.description || s.name
      return { value: String(s.name), label: s.virtual ? `${name} (${t('nova.settings.picker_virtual')})` : name }
    })
  }
  if (!list) return null
  const choices = [{ value: '', label: t('_common.autodetect') }, ...list]
  const value = String(current ?? '')
  if (value && !choices.some((c) => c.value === value)) {
    choices.push({ value, label: t('nova.settings.picker_not_found', { value }) })
  }
  return choices
}

/** Encoder types the host reports as working, from /api/host/info, or null. */
export function hostEncoders() {
  const active = hostInfo.value?.encoders?.active
  return active ? [String(active)] : null
}

/** One-line summary of the detected encoder for the Encoder section, or ''. */
export function encoderSummary(t) {
  const info = hostInfo.value
  const enc = info?.encoders
  if (!enc?.active) return ''
  const gpu = info.gpu?.[0]?.name?.replace(/^NVIDIA\s+/i, '') || ''
  const codecs = (enc.codecs || []).join(', ')
  return t('nova.settings.encoder_detected', { encoder: String(enc.active).toUpperCase(), gpu, codecs })
}

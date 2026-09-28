/**
 * @file State for the Settings page: loads /api/config (and the log, to know which
 * encoders the host found), keeps a working copy and the last saved copy, and saves.
 */
import { computed, ref, shallowRef } from 'vue'
import { fetchJson, getConfig, postJson } from '../nova/api.js'
import { detectEncoders, parseLogs } from '../nova/logs.js'
import { applyRules } from './settings_schema.js'
import { apiFetch } from '../fetch_utils.js'
import { hostEncoders, useHostProbes } from './hostProbes.js'
import {
  DEFAULTS, allOptionKeys, buildSavePayload, changedKeys, clone, optionApplies, prepareConfig, validateOption,
} from './settings_model.js'

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

/**
 * @returns Settings form state and actions.
 */
export function useSettingsForm() {
  const config = ref(null)
  const saved = shallowRef(null)
  const platform = shallowRef('')
  const detectedEncoders = shallowRef([])
  const loading = shallowRef(true)
  const loadError = shallowRef(null)
  const saving = shallowRef(false)
  const restarting = shallowRef(false)
  const needsRestart = shallowRef(false)
  // Errors show once a field has been left (blur) or a save was attempted.
  const touched = ref(new Set())
  const submitted = shallowRef(false)

  async function load() {
    loading.value = true
    loadError.value = null
    try {
      const response = await getConfig(true)
      const prepared = prepareConfig(response)
      platform.value = prepared.platform
      config.value = prepared.config
      saved.value = clone(prepared.config)
    } catch (error) {
      loadError.value = error
    } finally {
      loading.value = false
    }
    // Prefer what the host reports directly; older hosts only say it in the log.
    await useHostProbes().ready
    const fromHost = hostEncoders()
    if (fromHost) {
      detectedEncoders.value = fromHost
      return
    }
    try {
      const response = await apiFetch('./api/logs')
      if (response.ok) detectedEncoders.value = detectEncoders(parseLogs(await response.text())).map((e) => e.type)
    } catch {
      detectedEncoders.value = []
    }
  }

  /** Keys with unsaved edits. */
  const dirtyKeys = computed(() => (config.value && saved.value ? changedKeys(config.value, saved.value) : []))

  /** Validation errors of options that currently apply, keyed by option. */
  const errors = computed(() => {
    const result = {}
    if (!config.value) return result
    for (const key of allOptionKeys()) {
      if (!optionApplies(key, config.value, platform.value)) continue
      const error = validateOption(key, config.value[key])
      if (error) result[key] = error
    }
    return result
  })

  /** Errors to show now: for touched fields, or all of them after a save attempt. */
  const visibleErrors = computed(() => {
    if (submitted.value) return errors.value
    return Object.fromEntries(Object.entries(errors.value).filter(([key]) => touched.value.has(key)))
  })

  /** Mark a field as visited so its error shows. */
  function touch(key) {
    if (!touched.value.has(key)) touched.value = new Set([...touched.value, key])
  }

  /**
   * Set one option and apply dependent-option rules.
   *
   * @param {string} key Option name.
   * @param {*} value New value.
   */
  function setValue(key, value) {
    config.value[key] = value
    applyRules(config.value, platform.value)
  }

  /** Put one option back to its default. */
  function resetValue(key) {
    setValue(key, clone(DEFAULTS[key]))
  }

  /** Drop every unsaved edit. */
  function discard() {
    config.value = clone(saved.value)
    touched.value = new Set()
    submitted.value = false
  }

  /**
   * Save the configuration.
   *
   * @returns {Promise<boolean>} True when the host accepted it; false when a setting is invalid
   *          (all errors are then shown). Throws when the request fails.
   */
  async function save({ restartLater = true } = {}) {
    if (Object.keys(errors.value).length) {
      submitted.value = true
      return false
    }
    saving.value = true
    try {
      await postJson('./api/config', buildSavePayload(config.value))
      saved.value = clone(config.value)
      getConfig(true).catch(() => null)
      if (restartLater) needsRestart.value = true
      touched.value = new Set()
      submitted.value = false
      return true
    } finally {
      saving.value = false
    }
  }

  /**
   * Restart the host and wait until it answers again (up to ~45 s).
   *
   * @returns {Promise<boolean>} True when the host came back.
   */
  async function restart() {
    restarting.value = true
    needsRestart.value = false
    // The host drops the connection while restarting, so a failed response is expected.
    await apiFetch('./api/restart', { method: 'POST', headers: { 'Content-Type': 'application/json' } }).catch(() => null)
    const deadline = Date.now() + 45000
    await wait(3000)
    while (Date.now() < deadline) {
      try {
        await fetchJson('./api/config')
        restarting.value = false
        return true
      } catch {
        await wait(1500)
      }
    }
    restarting.value = false
    return false
  }

  /**
   * Save, then restart the host so the new settings apply.
   *
   * @returns {Promise<boolean|null>} True when saved and back up, false when the restart timed
   *          out, null when saving failed validation.
   */
  async function saveAndRestart() {
    const ok = await save({ restartLater: false })
    if (!ok) return null
    return restart()
  }

  return {
    config, platform, detectedEncoders, loading, loadError, saving, restarting, needsRestart,
    dirtyKeys, errors, visibleErrors, touch, load, setValue, resetValue, discard, save, saveAndRestart, restart,
  }
}

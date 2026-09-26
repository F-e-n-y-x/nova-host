/**
 * @file Paired-device state and actions for the Devices page.
 *
 * Changes are applied to the list right away and rolled back if the host refuses them;
 * actions re-throw the error so the caller can tell the user.
 */
import { computed, shallowRef } from 'vue'
import * as api from './deviceApi'
import { sortDevices } from './format'

/**
 * Paired devices with load state and actions.
 *
 * @returns {object} `devices`, `loading`, `error`, `busy` (uuid → true while saving),
 *   `byUuid(uuid)`, and the actions `reload`, `setEnabled`, `rename`, `setPermissions`,
 *   `disconnect`, `unpair`, `unpairAll`.
 */
export function useDevices() {
  const list = shallowRef([])
  const loading = shallowRef(true)
  const error = shallowRef(null)
  const busy = shallowRef({})

  const devices = computed(() => sortDevices(list.value))

  /**
   * Find a device by ID.
   *
   * @param {string} uuid Device ID.
   * @returns {object|null} The device, or null.
   */
  function byUuid(uuid) {
    return list.value.find((d) => d.uuid === uuid) || null
  }

  /** Load the list from the host. Keeps the old list visible while refreshing. */
  async function reload() {
    loading.value = true
    error.value = null
    try {
      list.value = await api.listDevices()
    } catch (e) {
      error.value = e
    } finally {
      loading.value = false
    }
  }

  /**
   * Replace one device's fields in the list.
   *
   * @param {string} uuid Device ID.
   * @param {object} fields Fields to overwrite.
   */
  function patchLocal(uuid, fields) {
    list.value = list.value.map((d) => (d.uuid === uuid ? { ...d, ...fields } : d))
  }

  /**
   * Mark a device as saving while an action runs.
   *
   * @param {string} uuid Device ID.
   * @param {() => Promise<any>} action Work to do.
   * @returns {Promise<any>} The action's result.
   */
  async function withBusy(uuid, action) {
    busy.value = { ...busy.value, [uuid]: true }
    try {
      return await action()
    } finally {
      const next = { ...busy.value }
      delete next[uuid]
      busy.value = next
    }
  }

  /**
   * Apply a change optimistically and roll it back when the host refuses.
   *
   * @param {string} uuid Device ID.
   * @param {object} optimisticFields Fields to show right away.
   * @param {() => Promise<any>} request Host call.
   */
  async function optimistic(uuid, optimisticFields, request) {
    const before = byUuid(uuid)
    if (!before) throw new Error('Unknown device')
    const previous = Object.fromEntries(Object.keys(optimisticFields).map((k) => [k, before[k]]))
    patchLocal(uuid, optimisticFields)
    try {
      await withBusy(uuid, request)
    } catch (e) {
      patchLocal(uuid, previous)
      throw e
    }
  }

  /**
   * Allow or block a device. Blocking also ends its stream on the host.
   *
   * @param {string} uuid Device ID.
   * @param {boolean} enabled New state.
   */
  async function setEnabled(uuid, enabled) {
    const wasConnected = byUuid(uuid)?.connected
    await optimistic(uuid, { enabled, connected: enabled ? wasConnected : false }, () => api.updateDevice(uuid, { enabled }))
  }

  /**
   * Rename a device.
   *
   * @param {string} uuid Device ID.
   * @param {string} name New name (already validated).
   */
  async function rename(uuid, name) {
    await optimistic(uuid, { name }, () => api.updateDevice(uuid, { name }))
  }

  /**
   * Change a device's permissions.
   *
   * @param {string} uuid Device ID.
   * @param {object} update Permission update: `{preset}` and/or individual flags.
   * @param {object} next The full permission object to show while saving.
   */
  async function setPermissions(uuid, update, next) {
    await optimistic(uuid, { permissions: next }, () => api.updateDevice(uuid, { permissions: update }))
  }

  /**
   * End a device's stream.
   *
   * @param {string} uuid Device ID.
   */
  async function disconnect(uuid) {
    await optimistic(uuid, { connected: false }, () => api.disconnectDevice(uuid))
  }

  /**
   * Unpair one device and drop it from the list.
   *
   * @param {string} uuid Device ID.
   */
  async function unpair(uuid) {
    await withBusy(uuid, () => api.unpairDevice(uuid))
    list.value = list.value.filter((d) => d.uuid !== uuid)
  }

  /** Unpair every device. */
  async function unpairAll() {
    await api.unpairAllDevices()
    list.value = []
  }

  reload()

  return { devices, loading, error, busy, byUuid, reload, setEnabled, rename, setPermissions, disconnect, unpair, unpairAll }
}

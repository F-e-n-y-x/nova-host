/**
 * @file Windows virtual input state: installed driver versions, the latest stable
 * Virtual HID Driver release, and the Virtual HID Driver license with its actions.
 */
import { computed, readonly, ref, shallowRef } from 'vue'
import { apiFetch } from '../../../fetch_utils'
import { compareDriverVersions } from './driverVersion'

export const VIRTUALHID_RELEASES = 'https://github.com/LizardByte/libvirtualhid/releases/latest'
export const VIGEMBUS_RELEASES = 'https://github.com/nefarius/ViGEmBus/releases/latest'
const RELEASE_API = 'https://api.github.com/repos/LizardByte/libvirtualhid/releases/latest'

const emptyDriver = () => ({ installed: false, version: '', version_compatible: false, minimum_version: '', supported_versions: '' })

/**
 * Release comparison state for an installed driver.
 *
 * @param {object} driver Installed driver status.
 * @param {{loading: boolean, error: boolean, version: string}} release Latest release.
 * @returns {'loading'|'unavailable'|'not_installed'|'unknown'|'current'|'outdated'} State.
 */
export function releaseState(driver, release) {
  if (release.loading) return 'loading'
  if (release.error || !release.version) return 'unavailable'
  if (!driver.installed) return 'not_installed'
  const comparison = compareDriverVersions(driver.version, release.version)
  if (comparison === null) return 'unknown'
  return comparison >= 0 ? 'current' : 'outdated'
}

/**
 * Virtual input driver and license state.
 *
 * @param {object} options Options.
 * @param {() => string} options.gamepadDriver Getter for the configured gamepad driver.
 * @param {(key: string) => string} options.translate Translator for request-failure text.
 * @returns {object} Read-only state and actions.
 */
export function useVirtualInput({ gamepadDriver, translate }) {
  const virtualhid = ref(emptyDriver())
  const vigembus = ref(emptyDriver())
  const release = ref({ loading: false, version: '', url: VIRTUALHID_RELEASES, error: false })
  const license = ref({
    operation_ok: false, service_available: false, state: 'unavailable', licensed: false, active_devices: 0,
    activation_limit: 0, activation_usage: 0, plan_name: '', customer_email: '', message: '', purchase_url: '', manage_account_url: '',
  })
  const licenseBusy = shallowRef(false)
  const licenseError = shallowRef('')

  const showVirtualhid = computed(() => gamepadDriver() !== 'vigembus')
  const showVigembus = computed(() => gamepadDriver() !== 'virtualhid')

  async function refreshStatus() {
    try {
      const r = await (await fetch('./api/virtual-input/status')).json()
      virtualhid.value = { ...emptyDriver(), ...(r.virtualhid || {}) }
      vigembus.value = { ...emptyDriver(), ...(r.vigembus || {}) }
    } catch (error) {
      console.error('Failed to fetch virtual input driver status:', error)
    }
  }

  async function refreshRelease() {
    if (!showVirtualhid.value) return
    release.value = { ...release.value, loading: true, error: false }
    try {
      const response = await fetch(RELEASE_API, { headers: { Accept: 'application/vnd.github+json' } })
      if (!response.ok) throw new Error(`GitHub returned ${response.status}`)
      const data = await response.json()
      if (!data.tag_name || data.draft || data.prerelease) throw new Error('GitHub did not return a stable release')
      release.value = { loading: false, version: data.tag_name, url: data.html_url || VIRTUALHID_RELEASES, error: false }
    } catch {
      release.value = { loading: false, version: '', url: VIRTUALHID_RELEASES, error: true }
    }
  }

  function refreshDrivers() {
    refreshStatus()
    refreshRelease()
  }

  function applyLicense(status) {
    license.value = { ...license.value, ...status }
    licenseError.value = status.error || ''
  }

  async function licenseRequest(options) {
    const response = await (options ? apiFetch('./api/virtual-input/license', options) : fetch('./api/virtual-input/license'))
    const status = await response.json()
    if (!response.ok) throw new Error(status.error || translate('virtualhid_license_request_failed'))
    return status
  }

  async function withLicenseBusy(task) {
    licenseBusy.value = true
    licenseError.value = ''
    try {
      return await task()
    } catch (error) {
      licenseError.value = error.message
      return null
    } finally {
      licenseBusy.value = false
    }
  }

  function refreshLicense() {
    return withLicenseBusy(async () => applyLicense(await licenseRequest()))
  }

  /**
   * Validate, activate or deactivate the license.
   *
   * @param {'validate'|'activate'|'deactivate'} action License action.
   * @param {string} [key] License key for activation.
   * @returns {Promise<boolean>} Whether the service reported success.
   */
  async function updateLicense(action, key = '') {
    if (licenseBusy.value || (action === 'activate' && !key.trim())) return false
    const body = { action }
    if (action === 'activate') body.license_key = key.trim()
    const status = await withLicenseBusy(() => licenseRequest({
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
    }))
    body.license_key = ''
    if (!status) return false
    applyLicense(status)
    return Boolean(status.operation_ok)
  }

  return {
    virtualhid: readonly(virtualhid), vigembus: readonly(vigembus), release: readonly(release), license: readonly(license),
    licenseBusy: readonly(licenseBusy), licenseError: readonly(licenseError), showVirtualhid, showVigembus,
    refreshDrivers, refreshLicense, updateLicense,
  }
}

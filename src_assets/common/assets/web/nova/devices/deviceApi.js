/**
 * @file Host API calls for paired devices and pairing requests.
 *
 * Every function resolves with the parsed reply or throws an Error whose `message` is the
 * host's error text when it sent one, and whose `status` is the HTTP status.
 */
import { apiFetch } from '../../fetch_utils'

/**
 * Call the host API and parse the JSON reply.
 *
 * @param {string} url Relative API URL.
 * @param {string} [method] HTTP method.
 * @param {object} [body] JSON body.
 * @returns {Promise<object>} Parsed reply.
 */
async function call(url, method = 'GET', body = undefined) {
  const options = { method }
  if (body !== undefined) {
    options.headers = { 'Content-Type': 'application/json' }
    options.body = JSON.stringify(body)
  }
  const response = await apiFetch(url, options)
  let data = {}
  try {
    data = await response.json()
  } catch {
    data = {}
  }
  if (!response.ok || data.status === false) {
    const error = new Error(data.error || `${response.status} ${response.statusText}`.trim())
    error.status = response.status
    throw error
  }
  return data
}

/**
 * Paired devices.
 *
 * @returns {Promise<Array<object>>} Devices as the host lists them (name, uuid, enabled,
 *   permissions, paired_at, last_connected_at, connected).
 */
export async function listDevices() {
  return (await call('./api/clients/list')).named_certs || []
}

/**
 * Change a device. Only the fields given are sent.
 *
 * @param {string} uuid Device ID.
 * @param {{enabled?: boolean, name?: string, permissions?: object}} patch Fields to change.
 * @returns {Promise<object>} Host reply.
 */
export function updateDevice(uuid, patch) {
  return call('./api/clients/update', 'POST', { uuid, ...patch })
}

/**
 * End a device's stream; its app keeps running.
 *
 * @param {string} uuid Device ID.
 * @returns {Promise<object>} Host reply.
 */
export function disconnectDevice(uuid) {
  return call('./api/clients/disconnect', 'POST', { uuid })
}

/**
 * Unpair one device.
 *
 * @param {string} uuid Device ID.
 * @returns {Promise<object>} Host reply.
 */
export function unpairDevice(uuid) {
  return call('./api/clients/unpair', 'POST', { uuid })
}

/**
 * Unpair every device.
 *
 * @returns {Promise<object>} Host reply.
 */
export function unpairAllDevices() {
  return call('./api/clients/unpair-all', 'POST', {})
}

/**
 * Pairing requests waiting for a PIN.
 *
 * @returns {Promise<Array<{id: string, name: string, address: string}>>} Requests.
 */
export async function listPairingRequests() {
  return (await call('./api/pin')).pairings || []
}

/**
 * Approve a pairing request with the PIN the device shows. Resolves once the device finishes
 * the handshake; throws when the PIN is wrong or the request expired.
 *
 * @param {string} pairingId Request ID.
 * @param {string} pin 4-digit PIN.
 * @param {string} name Name to give the device.
 * @returns {Promise<object>} Host reply.
 */
export function approvePairing(pairingId, pin, name) {
  return call('./api/pin', 'POST', { pairing_id: pairingId, pin, name })
}

/**
 * Decline a pairing request.
 *
 * @param {string} pairingId Request ID.
 * @returns {Promise<object>} Host reply.
 */
export function declinePairing(pairingId) {
  return call('./api/pin', 'DELETE', { pairing_id: pairingId })
}

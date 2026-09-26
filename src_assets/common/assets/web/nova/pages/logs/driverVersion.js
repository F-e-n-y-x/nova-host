/**
 * @file Dotted driver/release version comparison for the Windows virtual input panel.
 */

/**
 * Numeric parts of a dotted version.
 *
 * @param {string} version Version, optionally prefixed with "v".
 * @returns {number[]|null} Parts, or null when it isn't a version.
 */
export function parseDriverVersion(version) {
  const match = /^v?(\d+(?:\.\d+)*)/i.exec(String(version || '').trim())
  return match ? match[1].split('.').map(Number) : null
}

/**
 * Compare an installed driver version with the latest release.
 *
 * @param {string} installed Installed version.
 * @param {string} latest Latest release version.
 * @returns {number|null} Negative when outdated, 0 when equal, positive when newer, null when either is invalid.
 */
export function compareDriverVersions(installed, latest) {
  const a = parseDriverVersion(installed)
  const b = parseDriverVersion(latest)
  if (!a || !b) return null
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    const x = a[i] || 0
    const y = b[i] || 0
    if (x !== y) return x > y ? 1 : -1
  }
  return 0
}

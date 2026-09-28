/**
 * @file Pure helpers for host commands (the `host_commands` setting and an app's `menu-cmd`).
 * The host validates the same rules again when it saves (src/host_commands.cpp).
 */

/** Icon names the host and Nebula understand, in menu order. */
export const HOST_COMMAND_ICONS = ['terminal', 'refresh', 'power', 'lock', 'volume', 'mic-off', 'monitor', 'gamepad', 'stop', 'play', 'folder', 'settings']

/**
 * A new, empty command row.
 *
 * @returns {{id: string, name: string, icon: string, cmd: string, confirm: boolean, timeout: number}} Row.
 */
export function newHostCommand() {
  return { id: '', name: '', icon: 'terminal', cmd: '', confirm: false, timeout: 30 }
}

/**
 * Why a command line can't be saved: empty, or a quote left open (the host splits it into
 * arguments like a shell would, without running a shell).
 *
 * @param {string} line Command line.
 * @returns {'empty'|'quote'|null} Error kind, or null when it is fine.
 */
export function commandLineError(line) {
  const text = (line || '').trim()
  if (!text) return 'empty'
  let quote = null
  for (let i = 0; i < text.length; i++) {
    const c = text[i]
    if (quote === "'") {
      if (c === "'") quote = null
    } else if (quote === '"') {
      if (c === '\\' && (text[i + 1] === '"' || text[i + 1] === '\\')) i++
      else if (c === '"') quote = null
    } else if (c === '\\') {
      i++
    } else if (c === "'" || c === '"') {
      quote = c
    }
  }
  return quote ? 'quote' : null
}

/**
 * Whether a list has an entry the host would reject.
 *
 * @param {Array<object>} list Commands.
 * @returns {boolean} True when some row lacks a name or has a bad command line.
 */
export function hasInvalidCommand(list) {
  return (Array.isArray(list) ? list : []).some((c) => !c.name?.trim() || commandLineError(c.cmd))
}

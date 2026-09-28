import { afterEach, describe, expect, it, vi } from 'vitest'

import { apiFetch, checkBuild, resetBuildCheck } from '../../../src_assets/common/assets/web/fetch_utils.js'

function reply(build) {
  const headers = build ? { 'X-Nova-Build': build } : {}
  return new Response('{}', { status: 200, headers: { 'Content-Type': 'application/json', ...headers } })
}

function memoryStorage() {
  const data = new Map()
  return { getItem: (k) => (data.has(k) ? data.get(k) : null), setItem: (k, v) => data.set(k, String(v)) }
}

afterEach(() => {
  resetBuildCheck()
  vi.unstubAllGlobals()
})

describe('stale Web UI detection', () => {
  it('remembers the first build and ignores responses without the header', () => {
    const reload = vi.fn()
    expect(checkBuild(reply(null), { reload })).toBe(false)
    expect(checkBuild(reply('aaaa'), { reload })).toBe(false)
    expect(checkBuild(reply('aaaa'), { reload })).toBe(false)
    expect(reload).not.toHaveBeenCalled()
  })

  it('reloads once when Nova restarts with another Web UI build', () => {
    const reload = vi.fn()
    const storage = memoryStorage()
    checkBuild(reply('old'), { reload, storage })
    expect(checkBuild(reply('new'), { reload, storage })).toBe(true)
    expect(reload).toHaveBeenCalledTimes(1)
    // The reloaded page still sees a mismatch (e.g. a cached index): no loop.
    resetBuildCheck()
    checkBuild(reply('older'), { reload, storage })
    expect(checkBuild(reply('new'), { reload, storage })).toBe(false)
    expect(reload).toHaveBeenCalledTimes(1)
  })

  it('checks every apiFetch response', async () => {
    sessionStorage.clear()
    const builds = ['one', 'one', 'two']
    vi.stubGlobal('fetch', vi.fn(async () => reply(builds.shift())))
    await apiFetch('./api/pin')
    await apiFetch('./api/pin')
    expect(sessionStorage.getItem('nova-build-reloaded')).toBeNull()
    await apiFetch('./api/pin') // jsdom can't navigate; the reload marker shows it was attempted
    expect(sessionStorage.getItem('nova-build-reloaded')).toBe('two')
  })
})

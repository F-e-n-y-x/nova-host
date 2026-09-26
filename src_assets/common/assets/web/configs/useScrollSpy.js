/**
 * @file Track which page section is in view, for highlighting its link in a section nav.
 */
import { onBeforeUnmount, shallowRef, watch } from 'vue'

/**
 * @param {import('vue').Ref<string[]>} ids Element ids to watch, in page order.
 * @returns {{active: import('vue').ShallowRef<string>}} Id of the section nearest the top.
 */
export function useScrollSpy(ids) {
  const active = shallowRef('')
  let observer = null
  const visible = new Set()

  function pick() {
    const first = ids.value.find((id) => visible.has(id))
    if (first) active.value = first
  }

  function observe(list) {
    observer?.disconnect()
    visible.clear()
    if (typeof IntersectionObserver === 'undefined') return
    // A band near the top of the viewport: the section crossing it is the current one.
    observer = new IntersectionObserver((entries) => {
      for (const entry of entries) {
        if (entry.isIntersecting) visible.add(entry.target.id)
        else visible.delete(entry.target.id)
      }
      pick()
    }, { rootMargin: '-10% 0px -70% 0px' })
    for (const id of list) {
      const el = document.getElementById(id)
      if (el) observer.observe(el)
    }
    if (!active.value && list.length) active.value = list[0]
  }

  // Re-observe only when the set of sections changes, not on every recomputation.
  watch(() => ids.value.join('|'), () => requestAnimationFrame(() => observe(ids.value)), { immediate: true, flush: 'post' })
  onBeforeUnmount(() => observer?.disconnect())

  return { active }
}

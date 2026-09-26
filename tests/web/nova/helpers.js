import { mount } from '@vue/test-utils'
import { createI18n } from 'vue-i18n'
import { createMemoryHistory, createRouter } from 'vue-router'

import en from '../../../src_assets/common/assets/web/public/assets/locale/en.json'

/**
 * Mount a component with the app's i18n setup and a memory router.
 *
 * @param {object} component Component to mount.
 * @param {object} [options] @vue/test-utils mount options.
 * @returns {import('@vue/test-utils').VueWrapper} The wrapper.
 */
export function mountNova(component, options = {}) {
  const i18n = createI18n({ locale: 'en', fallbackLocale: 'en', messages: { en } })
  const router = createRouter({
    history: createMemoryHistory(),
    routes: [{ path: '/:pathMatch(.*)*', component: { template: '<div />' } }],
  })
  return mount(component, {
    attachTo: document.body,
    ...options,
    global: { plugins: [i18n, router], ...(options.global || {}) },
  })
}

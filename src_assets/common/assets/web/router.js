import { createRouter, createWebHistory } from 'vue-router'

/**
 * Web UI routes rendered from the single Vite entry page.
 *
 * `meta.bare` routes render without the app shell (first-run setup, signed-out page).
 * Old paths redirect to their new names, keeping any #anchor.
 */
const keep = (path) => (to) => ({ path, hash: to.hash, query: to.query })

const routes = [
  { path: '/', component: () => import('./nova/pages/Dashboard.vue') },
  { path: '/apps', component: () => import('./Apps.vue') },
  { path: '/devices', component: () => import('./nova/pages/Devices.vue') },
  { path: '/settings', component: () => import('./Config.vue') },
  { path: '/logs', component: () => import('./Troubleshooting.vue') },
  { path: '/pair', component: () => import('./Pin.vue') },
  { path: '/help', component: () => import('./nova/pages/Help.vue') },
  { path: '/help/clients', component: () => import('./Featured.vue') },
  { path: '/password', component: () => import('./Password.vue') },
  { path: '/welcome', component: () => import('./Welcome.vue'), meta: { bare: true } },
  { path: '/logout', component: () => import('./Logout.vue'), meta: { bare: true } },
  { path: '/config', redirect: keep('/settings') },
  { path: '/troubleshooting', redirect: keep('/logs') },
  { path: '/pin', redirect: keep('/pair') },
  { path: '/featured', redirect: keep('/help/clients') },
  { path: '/clients', redirect: keep('/devices') },
  { path: '/:pathMatch(.*)*', redirect: '/' },
]

const router = createRouter({
  history: createWebHistory(),
  routes,
  scrollBehavior(to) {
    return to.hash ? { el: to.hash } : { top: 0 }
  },
})

router.afterEach((to, from) => {
  // Move focus to the page content after in-app navigation so screen readers start there.
  if (from.matched.length && to.path !== from.path && !to.hash) {
    requestAnimationFrame(() => document.getElementById('nv-main')?.focus({ preventScroll: true }))
  }
})

export default router

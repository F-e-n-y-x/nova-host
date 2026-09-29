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
  { path: '/library', component: () => import('./Apps.vue') },
  { path: '/devices/:uuid?', component: () => import('./nova/pages/Devices.vue') },
  { path: '/browser', component: () => import('./nova/pages/BrowserPlay.vue') },
  { path: '/settings', component: () => import('./Config.vue') },
  { path: '/logs', component: () => import('./nova/pages/Logs.vue') },
  { path: '/pair', component: () => import('./Pin.vue') },
  { path: '/help', component: () => import('./nova/pages/Help.vue') },
  { path: '/help/clients', redirect: { path: '/help', hash: '#clients' } },
  { path: '/password', component: () => import('./Password.vue') },
  { path: '/welcome', component: () => import('./Welcome.vue'), meta: { bare: true } },
  { path: '/login', component: () => import('./Login.vue'), meta: { bare: true } },
  { path: '/logout', component: () => import('./Logout.vue'), meta: { bare: true } },
  { path: '/apps', redirect: keep('/library') },
  { path: '/config', redirect: keep('/settings') },
  { path: '/troubleshooting', redirect: keep('/logs') },
  { path: '/pin', redirect: keep('/pair') },
  { path: '/featured', redirect: { path: '/help', hash: '#clients' } },
  { path: '/clients', redirect: keep('/devices') },
  // Developer-only component gallery; compiled in only for dev or VITE_NOVA_GALLERY=1 builds.
  ...(import.meta.env.DEV || import.meta.env.VITE_NOVA_GALLERY === '1'
    ? [{ path: '/__components', component: () => import('./nova/pages/dev/ComponentGallery.vue') }]
    : []),
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
  // Only between pages: a detail panel on the same page (/devices/:uuid) manages its own focus.
  if (from.matched.length && to.matched[0] !== from.matched[0] && !to.hash) {
    requestAnimationFrame(() => document.getElementById('nv-main')?.focus({ preventScroll: true }))
  }
})

export default router

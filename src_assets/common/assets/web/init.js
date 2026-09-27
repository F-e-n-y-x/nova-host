import { applyConfiguredLocale, createInstantI18n } from './locale'

import '@fontsource/geist-sans/400.css'
import '@fontsource/geist-sans/500.css'
import '@fontsource/geist-sans/600.css'
import '@fontsource/geist-mono/400.css'
import '@fontsource/geist-mono/500.css'

export function initApp(app, config) {
    // Mount immediately in English; a slow or unreachable host must not leave the page blank.
    const i18n = createInstantI18n();
    app.use(i18n);
    app.provide('i18n', i18n.global)
    app.mount('#app');
    if (config) {
        config(app)
    }
    applyConfiguredLocale(i18n);
}

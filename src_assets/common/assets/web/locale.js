import {createI18n} from "vue-i18n";

// Import only the fallback language files
import en from './public/assets/locale/en.json'

const localePaths = new Map([
    ['bg', './assets/locale/bg.json'],
    ['cs', './assets/locale/cs.json'],
    ['de', './assets/locale/de.json'],
    ['en_GB', './assets/locale/en_GB.json'],
    ['en_US', './assets/locale/en_US.json'],
    ['es', './assets/locale/es.json'],
    ['fr', './assets/locale/fr.json'],
    ['hu', './assets/locale/hu.json'],
    ['it', './assets/locale/it.json'],
    ['ja', './assets/locale/ja.json'],
    ['ko', './assets/locale/ko.json'],
    ['pl', './assets/locale/pl.json'],
    ['pt', './assets/locale/pt.json'],
    ['pt_BR', './assets/locale/pt_BR.json'],
    ['ru', './assets/locale/ru.json'],
    ['sv', './assets/locale/sv.json'],
    ['tr', './assets/locale/tr.json'],
    ['uk', './assets/locale/uk.json'],
    ['vi', './assets/locale/vi.json'],
    ['zh', './assets/locale/zh.json'],
    ['zh_TW', './assets/locale/zh_TW.json'],
]);

/**
 * @brief Resolve a configured locale to a bundled translation file.
 *
 * @param {unknown} configuredLocale Locale returned by the configuration API.
 * @return {string} Supported locale identifier or the English fallback.
 */
function resolveLocale(configuredLocale) {
    if (typeof configuredLocale !== 'string') {
        return 'en';
    }

    return configuredLocale === 'en' || localePaths.has(configuredLocale) ? configuredLocale : 'en';
}

/**
 * @brief Load messages for a supported locale with an English fallback.
 *
 * @param {string} locale Supported locale identifier.
 * @param {Function} loadTranslation Function that loads a translation path.
 * @return {Promise<{locale: string, messages: object}>} Loaded locale and messages.
 */
export async function loadLocaleMessages(
    locale,
    loadTranslation = async path => (await fetch(path)).json(),
) {
    let messages = { en };

    try {
        if (locale !== 'en') {
            const translation = await loadTranslation(localePaths.get(locale));
            messages = Object.fromEntries([
                ['en', en],
                [locale, translation],
            ]);
        }
    } catch (e) {
        console.error("Failed to download translations", e);
        locale = 'en';
    }

    return { locale, messages };
}

/**
 * @brief Create the Vue internationalization instance for the configured locale.
 *
 * @return {Promise<import('vue-i18n').I18n>} Configured internationalization instance.
 */
export default async function createSunshineI18n() {
    const localeConfig = await (await fetch("./api/configLocale")).json();
    const configuredLocale = resolveLocale(localeConfig.locale);
    const { locale, messages } = await loadLocaleMessages(configuredLocale);

    document.documentElement.setAttribute('lang', locale);
    const i18n = createI18n({
        locale: locale, // set locale
        fallbackLocale: 'en', // set fallback locale
        messages: messages
    })
    return i18n;
}

/**
 * @brief Fetch JSON, giving up after a timeout so a slow host never blocks the UI.
 *
 * @param {string} path URL to fetch.
 * @param {number} timeoutMs Milliseconds before the request is aborted.
 * @return {Promise<object>} Parsed JSON.
 */
async function fetchJsonWithTimeout(path, timeoutMs) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    try {
        const response = await fetch(path, { signal: controller.signal });
        return await response.json();
    } finally {
        clearTimeout(timer);
    }
}

/**
 * @brief Create an English i18n instance synchronously, so the app can mount at once.
 *
 * @return {import('vue-i18n').I18n} Internationalization instance with English loaded.
 */
export function createInstantI18n() {
    document.documentElement.setAttribute('lang', 'en');
    return createI18n({ locale: 'en', fallbackLocale: 'en', messages: { en } });
}

/**
 * @brief Switch a mounted i18n instance to the host's configured locale, if any.
 *
 * Requests are bounded by `timeoutMs`; on any failure the UI stays in English.
 *
 * @param {import('vue-i18n').I18n} i18n Instance created by createInstantI18n().
 * @param {number} [timeoutMs] Per-request timeout in milliseconds.
 * @return {Promise<string>} The locale in use afterwards.
 */
export async function applyConfiguredLocale(i18n, timeoutMs = 2000) {
    try {
        const localeConfig = await fetchJsonWithTimeout("./api/configLocale", timeoutMs);
        const configuredLocale = resolveLocale(localeConfig.locale);
        if (configuredLocale === 'en') {
            return 'en';
        }
        const { locale, messages } = await loadLocaleMessages(
            configuredLocale,
            path => fetchJsonWithTimeout(path, timeoutMs),
        );
        if (locale === 'en') {
            return 'en';
        }
        const global = i18n.global;
        global.setLocaleMessage(locale, messages[locale]);
        if (global.locale && typeof global.locale === 'object') {
            global.locale.value = locale;
        } else {
            global.locale = locale;
        }
        document.documentElement.setAttribute('lang', locale);
        return locale;
    } catch (e) {
        console.error("Failed to load the configured locale", e);
        return 'en';
    }
}

// Must be the first import: it declares the cascade layer order for every other stylesheet.
import './nova/nova.css'

import { createApp } from 'vue'

import App from './App.vue'
import { initApp } from './init'
import router from './router'
import { loadAutoTheme } from './theme'

loadAutoTheme()

const app = createApp(App)
app.use(router)
initApp(app)

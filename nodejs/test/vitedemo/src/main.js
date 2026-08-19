// минимальный вход: проверяем, что глобальный vite собирает бандл без сети
// и без node_modules в проекте (rollup внутри vite подменен на wasm-сборку)
import { greeting } from './greeting.js'

document.querySelector('#app').textContent = greeting('astra')

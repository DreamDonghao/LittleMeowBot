import {defineConfig} from 'vite'
import vue from '@vitejs/plugin-vue'
import {fileURLToPath} from 'node:url'
import {resolve} from 'node:path'

const projectRoot = resolve(fileURLToPath(new URL('.', import.meta.url)), '..')

export default defineConfig({
    plugins: [vue()],
    build: {
        outDir: resolve(projectRoot, 'build/bot/public'),
        emptyOutDir: true
    },
    server: {
        proxy: {
            '/admin/api': {
                target: 'http://localhost:7778',
                changeOrigin: true
            },
            '/admin/ws': {
                target: 'ws://localhost:7778',
                ws: true
            }
        }
    }
})
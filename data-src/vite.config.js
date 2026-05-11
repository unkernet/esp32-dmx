import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import viteCompression from 'vite-plugin-compression';
import svgr from 'vite-plugin-svgr';
import { mockApi } from './mockApi';

const prod = process.env.NODE_ENV === 'production';

export default defineConfig({
  plugins: [
    svgr(),
    preact(),
    prod && viteCompression({
      algorithm: 'gzip',
      ext: '.gz',
      deleteOriginFile: true,
      threshold: 0,
    }),
    !prod && mockApi,
  ].filter(Boolean),
  build: {
    outDir: '../data',
    emptyOutDir: true,
    assetsDir: '',
    rollupOptions: {
      output: {
        entryFileNames: '[name].js',
        chunkFileNames: '[name].js',
        assetFileNames: '[name].[ext]'
      }
    }
  }
});

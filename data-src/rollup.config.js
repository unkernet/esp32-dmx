import resolve from '@rollup/plugin-node-resolve';
import commonjs from '@rollup/plugin-commonjs';
import terser from '@rollup/plugin-terser';
import nodePolyfills from 'rollup-plugin-polyfill-node';

export default {
  input: 'index.js',
  output: {
    file: '../data/index.js',
    format: 'iife',
    name: 'app',
  },
  plugins: [
    resolve(),
    commonjs(),
    nodePolyfills(),
    terser(),
  ],
};

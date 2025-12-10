import resolve from '@rollup/plugin-node-resolve';
import commonjs from '@rollup/plugin-commonjs';
import terser from '@rollup/plugin-terser';
import nodePolyfills from 'rollup-plugin-polyfill-node';

export default {
  input: 'index.js',
  output: {
    file: '../data/index.js', // Output uncompressed file
    format: 'iife', // Immediately Invoked Function Expression
    name: 'app',
  },
  plugins: [
    resolve(), // so Rollup can find `ms`
    commonjs(), // so Rollup can convert `ms` to an ES module
    nodePolyfills(),
    terser(),   // minify the output
  ],
};

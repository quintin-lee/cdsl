# CDSL WebAssembly & WASI Bridge

This directory contains the WebAssembly and WASI runtime bridge for **CDSL (C Domain Specific Language)**.

## Architecture

- **`cdsl_wasm.c`**: WASI/Emscripten exportable C bridge with JSON input and output.
- **`npm/` (`@cdsl/engine`)**: JavaScript / TypeScript SDK for Node.js, Web browsers, and Edge runtimes.
- **Proxy-Wasm Filter**: Can be compiled with `clang -target wasm32-wasi` to embed CDSL inside Envoy Proxy or API Gateways for sub-50 microsecond request filtering and traffic routing.

## Build for WebAssembly

```bash
emcc -O3 -I../include wasm/cdsl_wasm.c ../build/libcdsl.a -o cdsl.wasm \
  -s EXPORTED_FUNCTIONS='["_cdsl_wasm_eval_rule", "_cdsl_wasm_eval_dtable", "_cdsl_wasm_eval_scorecard", "_cdsl_wasm_eval_pipeline", "_cdsl_wasm_free"]' \
  -s STANDALONE_WASM
```

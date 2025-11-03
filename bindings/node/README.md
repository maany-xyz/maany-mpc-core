# Maany MPC Node Addon

This package exposes the Maany MPC C API to Node.js via a native N-API addon.

## Local Build & Test

1. Clean and rebuild the addon:

   ```bash
   cd bindings/node
   npm run prepare
   ```

   The `prepare` script configures CMake (`-DMAANY_BUILD_NODE_ADDON=ON`) in
   `../../build/node-addon`, builds the native library, and copies
   `maany_mpc_node.node` next to `index.js` so it can be required without a
   separate install step.

2. Run the smoke test, which exercises DKG → restore → sign → refresh end to end:

   ```bash
   npm test
   ```

   This simply executes `tests/node/roundtrip.js` using the locally built
   binding.

## Packaging for Consumers

To test the package as an npm dependency without publishing:

1. From `bindings/node`, build and pack:

   ```bash
   npm run prepare
   npm pack
   ```

   `npm pack` runs `prepare` automatically if necessary and produces a tarball
   such as `maany-mpc-node-0.1.0.tgz` containing the JS files and the compiled
   `.node` binary.

2. In your coordinator project, install the tarball:

   ```bash
   npm install /absolute/path/to/maany-mpc-node-0.1.0.tgz
   ```

3. Require `@maany/mpc-node` as usual; the packaged binary will be loaded via the
   built-in resolver in `index.js`.

## Clean Up

Remove build artifacts and the copied binary with:

```bash
npm run clean
```

This wipes `../../build/node-addon` and `bindings/node/maany_mpc_node.node` so
the next `npm run prepare` performs a fresh CMake configure/build cycle.

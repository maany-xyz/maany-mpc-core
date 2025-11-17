# @maany/mpc-coordinator-rn

React Native coordinator utilities for the Maany MPC stack. This package wraps the
`@maany/mpc-rn-bare` JSI binding and exposes the same high-level orchestration helpers
available in the Node coordinator so that in-app wallet SDKs can drive end-to-end
DKG, signing, and refresh flows entirely on-device.

## What It Provides

- `createCoordinator` – constructs a coordinator tied to a transport + share
  storage implementation and returns helpers to run DKG and signing rounds on top
  of the `@maany/mpc-rn-bare` binding.
- Session helpers – `runDkg` and `runSign` functions compatible with the Node
  coordinator semantics but implemented without Node built-ins.
- Cosmos utilities – `pubkeyToCosmosAddress`, `makeSignBytes`, and `sha256`
  built using `@noble/hashes` so they work in a React Native environment.
- In-memory adapters – simple `InMemoryTransport` and
  `InMemoryShareStorage` implementations for local testing.
- React Native `WebSocketTransport` to bridge the coordinator over a network
  connection using the platform WebSocket API.
- Secure storage helper – `SecureShareStorage` persists key shares in the iOS
  Keychain / Android Keystore, protecting them with biometric or device
  credential access before decrypting the share in memory.

## What Is Still Missing

- **Production-ready transport**: the included `WebSocketTransport` covers the
  happy path. For production you’ll likely want reconnection, auth, message
  framing, and backpressure handling on top.
- **Persistent storage hardening**: the provided `SecureShareStorage` focuses on
  per-device biometric gating. Production apps may still want wrapping policies
  (multi-user isolation, backup migration, etc.).
- **Refresh flow orchestration**: while `mpc.refreshNew` is exposed through the
  binding, the coordinator currently focuses on DKG and signing. Extending the
  RN coordinator with refresh helpers that mirror the Node package is next.
- **Error handling + retries**: production usage needs richer state management
  and retry policies around transport and MPC step errors.
- **Integration tests**: DKG/sign end-to-end tests running inside a RN app (or
  Jest + Hermes) to validate the full pipeline.

## Using Inside a React Native App

Install from the repo root (assuming you have access to the monorepo):

```bash
cd packages/coordinator-rn
npm install
npm run build
```

In your React Native app:

1. Add the coordinator (and peer dependencies) via file reference or after
   publishing to your registry.

   ```bash
   # Example: install via relative path from the app project
   npm install ../maany-mpc-core/packages/coordinator-rn
   npm install ../maany-mpc-core/bindings/rn
   ```

   Ensure `pod install` runs in the iOS project after adding `@maany/mpc-rn-bare`.

2. Initialize the coordinator:

   ```ts
   import {
     createCoordinator,
     InMemoryTransport,
     InMemoryShareStorage,
     SecureShareStorage,
     WebSocketTransport,
   } from '@maany/mpc-coordinator-rn';

   const coordinator = createCoordinator({
     transport: new InMemoryTransport(), // replace with a real transport in production
     storage: new SecureShareStorage({ promptMessage: 'Authenticate to unlock MPC share' }),
   });

   const ctx = coordinator.initContext();
   ```

3. Run DKG/sign sessions (e.g. in a mock environment where device/server
   share the same process):

   ```ts
   async function demo() {
     const { deviceKeypair, serverKeypair } = await coordinator.runDkg(ctx, {
       sessionId: new Uint8Array([1, 2, 3]),
     });

     const message = new Uint8Array(32);
     const signature = await coordinator.runSign(ctx, deviceKeypair, serverKeypair, {
       transport: coordinator.options.transport,
       message,
     });
     console.log('DER signature bytes:', signature);
   }
   ```

4. When integrating with a real coordinator service, swap in a real `Transport`:
   `WebSocketTransport.connect({ url: 'wss://your-coordinator', participant: 'device' })`
   will attach the device to a remote server, while the Node coordinator on the
   backend can keep using the existing WebSocket transport. Pair this with
   either the built-in `SecureShareStorage` or your own storage backed by
   platform secure enclaves.

## Development Notes

- Run `./scripts/build_ios_core.sh` in this repo before running `pod install`
  in your React Native app so the prebuilt XCFrameworks (Maany MPC core,
  cb-mpc, and OpenSSL) are staged under `bindings/rn/ios/dist`.
- Using the binding requires CocoaPods 1.12+; the `@maany/mpc-rn-bare` podspec now
  links the static archives, embeds OpenSSL, and installs the necessary build
  phase automatically, so consumer apps only need to run `pod install`.
- TypeScript builds target ES2020 and CommonJS for compatibility with Metro.
- The package ships ambient type shims for `react-native` so standalone builds
  work without pulling the full RN type definitions.
- The coordinator currently assumes both sides of the MPC session live inside
  the same runtime (suitable for local testing). For real deployments, connect
  the `Transport` to the backend coordinator, mirroring the Node package.
- Native secure storage relies on the iOS Keychain (iOS 13+) and Android
  Keystore + `androidx.biometric` (API 23+). Ensure the app is configured to
  support biometrics or device credentials.

## Next Steps

- Port the refresh/session management logic from the Node coordinator.
- Provide WebSocket/HTTP transports tailored to React Native.
- Add end-to-end tests using the RN binding (Hermes/Jest or Detox).

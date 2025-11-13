# Maany MPC Coordinator (Design Draft)

The coordinator sits above the core MPC library and implements end-to-end flows
for Cosmos-based signing. This document captures the intended responsibilities,
module layout, and immediate tasks so we can bootstrap the implementation inside
this repo before extracting it into its own package.

## Responsibilities

- **Session orchestration**: Drive DKG, signing, refresh, and restore using the
  `@maany/mpc-node` addon; manage retries, error reporting, and telemetry.
- **Share lifecycle**: Persist encrypted device/server shares via host-provided
  storage callbacks; coordinate import/export during restore flows.
- **Transport abstraction**: Define a pluggable interface for sending MPC
  messages between device and server (e.g., WebSocket, gRPC, or bespoke RPC).
- **Cosmos integration**: Translate MPC outputs into Cosmos primitives (address
  derivation, sign-doc canonicalization, Amino/Protobuf envelopes).
- **Policy enforcement**: Expose hooks for rate limits, approval workflows, or
  additional business logic before a signing session proceeds.

## Proposed Package Layout

```
packages/coordinator-node/
  package.json          # JS/TS coordinator package (depends on @maany/mpc-node)
  tsconfig.json
  src/
    index.ts            # Public API surface
    transport.ts        # Common transport interface (in-memory helper)
    transport/
      websocket.ts      # WebSocket-based transport for coordinator <-> SDK
    storage.ts          # Share persistence abstraction
    session/
      dkg.ts            # High-level DKG orchestration
      sign.ts           # Signing coordinator (incl. refresh)
    cosmos/
      address.ts        # Compressed pubkey -> Cosmos address helpers
      sign-doc.ts       # Canonical JSON / Protobuf envelope helpers
    util/
      logger.ts         # Thin logging abstraction
      errors.ts         # Coordinator-specific error types
```

The coordinator package can be built with TypeScript and published alongside the
addon once stabilized. For now the focus is to implement the orchestration logic
locally and dogfood it within the repository.

### WebSocket Handshake

The `CoordinatorServer` expects each device session to open a WebSocket and send
an initial JSON payload:

```
{
  "type": "hello",
  "sessionId": "unique-session-id",
  "role": "device",
  "token": "<auth token>",
  "intent": "dkg" | "sign" | "refresh",
  "keyId": "optional-key-id",
  "sessionIdHint": "optional-remote-session"
}
```

Once received, the server emits a `session:ready` event with the MPC transport,
context, and intent metadata so the application can kick off DKG/signing logic.

### Production Integration (Next Steps)

The package leaves the following pieces to the host application:

- **Authentication**: verify the `token` supplied in the handshake (JWT / API
  call) before running DKG or signing.
- **Persistent storage**: implement `ShareStorage` backed by your database or
  secret manager (encrypt shares, zeroize plaintext immediately after use).
- **Session handlers**: subscribe to `session:ready` and trigger
  `coordinator.runDkg` / `coordinator.runSign` depending on the provided intent.
- **Transport cleanup**: close sessions when flows complete / time out; apply
  retry or backoff policies as needed.
- **Logging & metrics**: capture audit logs, per-session telemetry, and alerts.

## Immediate Implementation Tasks

1. **Scaffold package**: create `package.json`, `tsconfig.json`, and TypeScript
   entry points under `src/`.
2. **Transport interface**: define a request/response abstraction that the host
   application can implement (e.g., `await transport.send(participant, message)`).
   A WebSocket implementation is available under `src/transport/websocket.ts`.
3. **Session orchestration**:
   - wrap the addon’s DKG API into a promise-based helper that automatically
     loops through `dkgStep` until completion.
   - encapsulate signing + optional refresh into a reusable `runSignSession`
     function with deterministic message exchange.
4. **Share storage**: specify an interface for encrypting/decrypting share blobs
   and provide a simple in-memory/file-system implementation for testing.
5. **Cosmos utilities**: implement pubkey → address derivation (SHA-256,
   RIPEMD-160, Bech32), and helpers to generate canonical sign bytes.
6. **End-to-end smoke test**: create a script under `tests/node` or a dedicated
   coordinator test directory that provisions shares, signs a dummy Cosmos
   message, and verifies the signature using `@cosmjs` or native crypto.

## Extraction Strategy

- Keep public exports limited to a single `src/index.ts` so the module boundary
  is well-defined.
- Avoid touching global state in the repository (no cross-package imports) so
  we can copy the directory to a new repo later.
- Once feature-complete, adjust the build scripts to publish the coordinator as
  a separate npm package, leaving the addon unchanged.

This living document should be updated as the coordinator evolves.

# Coordinator Implementation Notes

## Current Status
- Persistent storage backbone is live: coordinator now saves encrypted server shares + metadata via Prisma/Postgres, and session/nonce APIs exist (Redis adapter wired, though not yet used by the server runtime).
- AES-GCM key wrapping is in place with an env-key helper and a plaintext fallback for local smoke tests.
- Example Node coordinator server exercises the real storage/encryptor path and can be pointed at local Postgres/Redis for integration testing.

## Next Focus Areas

1. **Backup Artifact Intake & Recovery Flow**
   - Accept device-uploaded backup bundles (ciphertext + coordinator fragment) and persist them through the storage interface.
   - When recovery is requested, authenticate, fetch ciphertext + coordinator fragment, pull the recovery partner’s share, and invoke `backupRestore`.
   - Ensure the coordinator never accesses raw device shares outside of the MPC library; keep everything encrypted at rest.

2. **Session Persistence via Redis**
   - Thread `saveSession/getSession/deleteSession` into `CoordinatorServer` so session lifecycle survives process restarts.
   - Store minimal per-session metadata (sessionId, walletId, type, serialized state blob, expiresAt) and prune Redis keys on completion.
   - Expose metrics/logging for session eviction to detect hung or expired sessions.

3. **Signing Message Handling (TODO)**
   - Extend the WebSocket handshake (or follow-up RPC) so the device supplies the exact message/digest to sign.
   - Validate/authenticate the request server-side, persist any required context, then pass the provided bytes into `runSign` instead of the dummy digest.
   - Ensure both device and server call `signSetMessage` with identical payloads and consider including message hashes in audit logs.

4. **Nonce Tracking Hooks**
   - Integrate `getNonce/setNonce` around signing or protocol counters that require monotonicity.
   - Decide when to increment (e.g., after successful signatures) and enforce roll-forward semantics to guard against replay.

5. **Recovery Metadata & APIs**
   - Extend `WalletShareRecord` (and storage schema) if you need to capture additional identifiers (e.g., per-device backup IDs, policy tags).
   - Add coordinator APIs/endpoints for devices to upload/download backup fragments securely.

Document additional engineering decisions here as they arise so the coordinator/server implementations stay aligned.

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

# User Flow:
1. Core States (per app + user)

For a given (appId, userId) you basically have three meaningful states:

NO_WALLET

No entry on MPC servers

No local key share
→ Treat as “fresh user”

REMOTE_ONLY

MPC server has a wallet (and recovery artifacts)

Device has no local share yet
→ “Existing wallet, new/clean device → recovery flow”

LOCAL + REMOTE

Device has its local share (encrypted in Keychain/Keystore)

MPC server has its share + recovery artifacts
→ “Normal usage”

Your SDK logic is basically a state machine moving between these.

2. Registration → Background Wallet Creation (DKG)
2.1. User registers / logs in

User registers or logs in in the app:

email + password / SSO / WebAuthn / whatever.

App backend authenticates and issues:

Auth token (JWT or session token).

Frontend initializes Maany SDK:

const maany = new MaanySDK({ appId, authToken });
await maany.init();

2.2. SDK init: local + remote discovery

SDK flow (simplified):

async function init() {
  const localShare = await storage.getLocalShare(appId, userId);

  if (localShare) {
    state = "LOCAL + REMOTE";      // Normal usage
    return;
  }

  // No local share → ask coordinator for wallet status
  const status = await coordinator.getWalletStatus({ appId, authToken });

  // status: { exists: boolean, createdAt?: string, ... }

  if (!status.exists) {
    // No wallet anywhere → background DKG
    await createNewWalletInBackground();
  } else {
    // Wallet exists remotely but not locally → recovery UX
    await showRecoveryUI(status);
  }
}

2.3. Background DKG (fresh user, NO_WALLET)

Case: no local share, no remote wallet.

Flow:

SDK calls coordinator:

POST /wallet/create with authToken + appId.

Coordinator:

Verifies authToken (JWT locally or via /auth/verify).

Generates its MPC share.

Runs DKG with device (or device-side library) to generate user/device share.

Stores:

keyId = appId + userId

coordinator share

encrypted recovery fragments (for coordinator + third party).

Device:

Receives its share.

Encrypts it with device key / biometric key.

Stores it in Keychain/Keystore (or secure local storage).

UX-wise you can keep this invisible or show something like:

“Setting up your Maany wallet…” → small spinner/toast.
Once done: → “Your wallet is ready.”

From that moment on, you are in LOCAL + REMOTE state.

3. Wallet Session UX (normal day-to-day use)

For a “normal” logged-in user on a device where the wallet exists:

User logs into app → gets authToken.

SDK init() finds local share → no network roundtrip needed for wallet existence.

For signing a transaction:

SDK checks:

Is user authenticated? (has authToken)

Is local share accessible? (biometric/pin unlock if you want)

Runs MPC signing protocol with coordinator:

Device uses local share.

Coordinator uses its share.

Optionally enforce:

Biometric every sign

Or: 1 biometric unlock → “wallet session” for X minutes

So you have two layers:

App session: controlled by app auth token (login).

Wallet session: controlled by device share + local biometric.

This cleanly separates “user is logged in” from “user is allowed to sign”.

4. Recovery UX (REMOTE_ONLY: existing user on new device)

This is your more complex bit. Let’s break it cleanly:

4.1. Detection of recovery situation

On a new device:

No local share

But getWalletStatus() returns exists: true for (appId, userId)

SDK tells the app:

“We detected an existing Maany wallet for this account.
Do you want to restore it on this device?”

Here you show a clear choice:

Restore existing wallet (recommended)

Create a brand new wallet (dangerous, usually for advanced users, maybe hidden behind “advanced”)

We’ll talk about the second option in section 5.

4.2. Recovery flow (happy path)

Assuming user picked Restore existing wallet:

Extra verification step (strongly recommended):

Email link or OTP, or WebAuthn confirmation

This is to make sure a stolen app session token isn’t enough to recover the wallet.

Example UX:

“We emailed you a recovery link. Please confirm to restore your wallet.”

Device sends recovery request to both servers:

POST /wallet/recover to Coordinator

POST /wallet/recover to Third-Party Recovery Server
Both requests include:

authToken (app-level)

recoveryToken (from email / WebAuthn / etc.)

appId, userId

Coordinator / third-party validate:

Check auth token (user is logged in).

Check second factor (email / passkey / etc.).

Confirm they’re allowed to release encrypted recovery fragments.

Servers return encrypted recovery blobs:

Device receives two blobs (coordinator fragment + third-party fragment).

Uses user’s provided recovery passphrase / device key to decrypt (depending on your model).

Reconstructs the device share.

Device stores new share locally:

Encrypted at rest (Keychain/Keystore).

Overwrites any stale partial state (if any).

(Optional) notify servers:

POST /wallet/recovered so both servers can log device association / metrics.

UX copy:

“Wallet recovered successfully 🎉
This device can now sign transactions.”

From here, state becomes LOCAL + REMOTE again
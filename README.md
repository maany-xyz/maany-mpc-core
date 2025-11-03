# Maany MPC Core

This repository provides a thin C API over Coinbase's cb-mpc library so that a
device and server share can perform distributed key generation (DKG) and
two-party ECDSA signing. The current focus is the secp256k1 curve and the
two-party ECDSA scheme used for Coinbase Wallet-as-a-Service.

## Prerequisites

- CMake 3.20+
- A C++17 toolchain (Clang on macOS, GCC or Clang on Linux)
- OpenSSL development headers and libraries (detected via `find_package`)
- The `cpp/third_party/cb-mpc` submodule checked out at `cb-mpc-v0.1.0`

Ensure submodules are present before configuring:

```bash
git submodule update --init --recursive
```

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

This produces the static library `libmaany_mpc_core.a` and the integration
exercise `dkg_roundtrip`.

## End-to-End Exercise

Run the signing-capable roundtrip from the build directory:

```bash
./build/dkg_roundtrip
```

The program performs:

1. DKG between device (party P1) and server (party P2) shares
2. Public-key equality verification
3. A complete two-party ECDSA signing flow
4. Signature verification using cb-mpc crypto primitives

The binary exits with status `0` on success and prints diagnostic messages on
failure.

## API Overview

### Context Lifecycle

```c
maany_mpc_ctx_t* ctx = maany_mpc_init(nullptr);
...
maany_mpc_shutdown(ctx);
```

The context owns callbacks for allocation, logging, and random generation that
are forwarded into the bridge layer.

### Distributed Key Generation

1. Create matching `maany_mpc_dkg_t*` handles for the device (`MAANY_MPC_SHARE_DEVICE`) and
   server (`MAANY_MPC_SHARE_SERVER`) using `maany_mpc_dkg_new`.
2. Exchange messages by alternating calls to `maany_mpc_dkg_step` until both
   sides yield `MAANY_MPC_STEP_DONE`.
3. Finalize each party’s local key share via `maany_mpc_dkg_finalize`, which
   produces `maany_mpc_keypair_t*` handles.
4. (Optional) Query public-key metadata with `maany_mpc_kp_pubkey` or persist
   shares using `maany_mpc_kp_export`.

### Two-Party Signing

1. Derive signing sessions for both parties with `maany_mpc_sign_new` using the
   keypairs from DKG.
2. Provide the pre-hashed message to each share via
   `maany_mpc_sign_set_message`.
3. Alternate calls to `maany_mpc_sign_step`, passing the latest outbound buffer
   to the peer until both return `MAANY_MPC_STEP_DONE`.
4. Call `maany_mpc_sign_finalize` **only on the device share (party P1)** to
   retrieve the signature. The server share does not expose signature bytes and
   will signal `MAANY_MPC_ERR_PROTO_STATE` if `Finalize` is invoked.
5. You may request either `MAANY_MPC_SIG_FORMAT_DER` or
   `MAANY_MPC_SIG_FORMAT_RAW_RS`; the device handle supports repeated calls for
   different formats without re-running the protocol.

### Share Refresh

1. For an existing local share, create a refresh session via
   `maany_mpc_refresh_new`. The API reuses the DKG step/finalize entry points.
2. Run the same round-robin loop using `maany_mpc_dkg_step` until both parties
   report `MAANY_MPC_STEP_DONE`.
3. Call `maany_mpc_dkg_finalize` on each refresh handle to obtain updated key
   shares. The resulting public key should match the original share.

The refresh API returns entirely new keypair handles; remember to free the old
handles once the application transitions to the refreshed shares.

### Memory Management

All buffers returned through the public API must be released with
`maany_mpc_buf_free`. Session, keypair, and DKG handles are freed with their
respective `*_free` functions. The bridge zeroes sensitive material after use.

## Known Limitations

- Only secp256k1 and the two-party ECDSA scheme are wired through the bridge.
- Refresh and HD key derivation APIs are stubbed and currently return
  `MAANY_MPC_ERR_UNSUPPORTED`.
- The signing implementation assumes messages are pre-hashed to the curve
  length, mirroring cb-mpc’s expectations.

## Contributing

Please run `cmake --build build -j` and `./build/dkg_roundtrip` before
submitting patches. Contributions that extend coverage (e.g., unit tests for the
C API) are especially welcome.

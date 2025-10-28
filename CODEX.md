0) Goals (scope for this milestone)

Protocol: ECDSA 2-of-2 only (secp256k1).

Targets: Node (N-API addon), React Native (JSI/TurboModule). (Browser/WASM later.)

Public ABI: Stable C API (maany_mpc_*) that wraps cb-mpc C++ and hides its types.

Bindings: TS facade with identical API for Node and RN.

No network & no at-rest encryption inside the bridge—storage and transport are the SDK’s job.

1) Repository structure (final)
maany-mpc-core/
  CMakeLists.txt
  CMakePresets.json
  cpp/
    include/
      maany_mpc.h               # public C ABI (stable)
      bridge.h                  # internal C++ façade (thin)
    src/
      maany_mpc.c               # C ABI -> bridge
      bridge.cpp                # bridge -> cb-mpc C++
      util.cc                   # helpers (DER encode, zeroize, buf marshal)
    third_party/
      cb-mpc/                   # submodule @ cb-mpc-v0.1.0
  bindings/
    node/
      CMakeLists.txt
      addon.cc                  # N-API -> maany_mpc_*
      package.json
      index.ts                  # TS facade loader
      tsconfig.json
    rn/
      android/
        CMakeLists.txt
        build.gradle
        src/main/cpp/addon_jni.cc     # JSI host object -> maany_mpc_*
      ios/
        MaanyMpc.mm                   # JSI/Turbo module
        MaanyMpc.podspec
      index.ts
      package.json
      tsconfig.json
  packages/
    mpc-core/
      src/index.ts               # Runtime selector: node vs rn
      package.json
      tsconfig.json
      README.md
  tests/
    node/
      roundtrip.spec.ts          # jest/vitest E2E against addon
    rn/
      roundtrip.spec.ts          # detox or jest + JSI direct call
  .github/workflows/
    ci.yml
    release.yml

2) Public C API (freeze this)

File: cpp/include/maany_mpc.h

Rules:

All output buffers are lib-allocated; caller frees with maany_mpc_buf_free.

kp_export returns an opaque blob (SDK encrypts/stores it; bridge doesn’t).

Public key is 33-byte compressed secp256k1.

3) Internal bridge API (C++)

File: cpp/include/bridge.h

File: cpp/src/bridge.cpp
Implement using cb-mpc ecdsa-2pc classes @ v0.1.0. Map:

Ctx holds cb-mpc global state (rng, OpenSSL handles if needed).

Dkg wraps 2-party DKG instance (device/server roles).

Keypair wraps the local share object/bytes.

Sign wraps 2-party signer instance.

Serialize/deserialize protocol messages to raw std::vector<uint8_t>.

Notes for Codex:

Use cb-mpc’s provided message structs/serialization helpers (if any). If not, use the repo’s demo encoding (protobuf/json) and keep it byte-compatible between device/server.

Ensure small-subgroup & parameter checks mirror cb-mpc demo patterns.

4) C ABI thunk

File: cpp/src/maany_mpc.c

Convert C buffers ⇄ std::vector<uint8_t>.

Convert handles to owning std::unique_ptr<> stored inside opaque structs.

Map bool/throw from bridge to maany_mpc_error_t.

Opaque structs (C side):

struct maany_mpc_ctx_s  { void* p; };
struct maany_mpc_dkg_s  { void* p; };
struct maany_mpc_kp_s   { void* p; };
struct maany_mpc_sign_s { void* p; };


Internally cast p to corresponding std::unique_ptr<maany::bridge::X>*.

5) Node binding (N-API)

Files: bindings/node/addon.cc, bindings/node/CMakeLists.txt, bindings/node/package.json

Exposed JS API (final)
export type Ctx = object & { __brand: 'ctx' };
export type Keypair = object & { __brand: 'kp' };
export type Dkg = object & { __brand: 'dkg' };
export type Sign = object & { __brand: 'sign' };

export function init(): Ctx;
export function shutdown(ctx: Ctx): void;

export function dkgNew(ctx: Ctx, role: 'device'|'server', keyId?: Uint8Array, sessionId?: Uint8Array): Dkg;
export function dkgStep(ctx: Ctx, dkg: Dkg, inPeerMsg?: Uint8Array): { outMsg?: Uint8Array; done: boolean };
export function dkgFinalize(ctx: Ctx, dkg: Dkg): Keypair;

export function kpExport(ctx: Ctx, kp: Keypair): Uint8Array;
export function kpImport(ctx: Ctx, blob: Uint8Array): Keypair;
export function kpPubkey(ctx: Ctx, kp: Keypair): Uint8Array; // 33 bytes

export function signNew(ctx: Ctx, kp: Keypair, opts?: { sessionId?: Uint8Array; aad?: Uint8Array }): Sign;
export function signSetMessage(ctx: Ctx, sign: Sign, msg: Uint8Array): void;
export function signStep(ctx: Ctx, sign: Sign, inPeerMsg?: Uint8Array): { outMsg?: Uint8Array; done: boolean };
export function signFinalize(ctx: Ctx, sign: Sign, fmt?: 'der'|'raw-rs'): Uint8Array;


N-API rules

Wrap native handles as napi_external with finalizers calling *_free.

Offload *_step and *_finalize to a libuv worker (async) to avoid blocking.

Throw JS errors mapping from maany_mpc_error_t.

6) React-Native binding (JSI/Turbo)

File highlights

bindings/rn/ios/MaanyMpc.mm: Registers JSI host object with methods mirroring Node API.

bindings/rn/android/src/main/cpp/addon_jni.cc: JNI + JSI binding; loads libmaany_mpc_core.so.

bindings/rn/index.ts: Same TS surface as Node.

Packaging

iOS: CocoaPod via MaanyMpc.podspec linking libmaany_mpc_core.a or .xcframework.

Android: Gradle builds shared lib for arm64-v8a (+ optional x86_64 for emulators).

7) CMake (top-level + presets)

File: CMakeLists.txt (root)

cmake_minimum_required(VERSION 3.22)
project(maany_mpc_core LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(cpp/third_party/cb-mpc EXCLUDE_FROM_ALL)

add_library(maany_mpc_core
  cpp/src/bridge.cpp
  cpp/src/maany_mpc.c
  cpp/src/util.cc
)
target_include_directories(maany_mpc_core PUBLIC cpp/include)
target_link_libraries(maany_mpc_core PRIVATE cbmpc) # name per cb-mpc cmake target


File: CMakePresets.json (native/mac + linux)

{
  "version": 3,
  "configurePresets": [
    { "name": "default", "generator": "Ninja", "binaryDir": "build/default" }
  ],
  "buildPresets": [
    { "name": "default", "configurePreset": "default" }
  ]
}


Node & RN subprojects add their own CMakeLists.txt that link against maany_mpc_core.

8) Test plan (must-pass)
Unit (C++)

DER encoding round trip for a known (r,s).

Compressed pubkey sanity (33 bytes, 0x02/0x03 prefix).

Opaque blob export/import idempotence.

E2E (Node)

Simulate device and server in one process:

DKG A↔B until both DONE, dkg_finalize() both.

kp_pubkey() equality check (both sides produce same pubkey).

sign_* A↔B for a fixed message; verify signature with a pure JS secp lib.

Negative tests: wrong role pairing; mutated message mid-round → expect MAANY_MPC_STATE/CRYPTO.

E2E (RN)

Same as Node but through JSI binding (can be a single “mock” test that runs both roles locally).

9) Build scripts & commands

First build (native lib):

git submodule update --init --recursive
cmake -S . -B build && cmake --build build -j


Node addon (from bindings/node):

Use cmake-js or a small CMakeLists.txt + node-gyp wrapper.

Produce prebuilds for darwin-arm64, linux-x64, linux-arm64.

RN

iOS: pod install inside a sample app; ensure the Pod links maany_mpc_core.

Android: Gradle builds .so under jniLibs/arm64-v8a.

10) Security & correctness requirements

Zeroization: wipe ephemeral secrets in bridge.cpp destructors and in maany_mpc_sign_free.

Constant-time: do not branch on secrets in wrapper logic; leave heavy math to cb-mpc.

Input validation: check buffer sizes (pubkey 33B, non-empty msg), role (device/server), protocol state.

Serialization: use cb-mpc’s provided message encoding if present; otherwise clearly document the encoding used so device/server are interoperable.

Versioning: expose a small maany_mpc_version() (ABI) and embed the pinned cb-mpc commit in maany_mpc_core as a string for telemetry.

11) Deliverables checklist for Codex

 cpp/include/maany_mpc.h (as above, minimal subset).

 cpp/include/bridge.h + cpp/src/bridge.cpp calling cb-mpc (ECDSA 2-party).

 cpp/src/maany_mpc.c exporting the C ABI.

 bindings/node N-API addon with TS facade (index.ts) matching the API table.

 bindings/rn JSI module (iOS/Android) with TS facade matching Node.

 packages/mpc-core loader that re-exports Node/RN bindings, same TS types.

 E2E tests proving A↔B DKG + sign produce valid sigs (verify with independent secp lib).

 CI matrix that builds the core lib, Node prebuilds (3 platforms), RN libs (iOS/Android), and runs Node tests.

12) Example TS usage (what SDK will call)
import * as mpc from '@maany/mpc-core';

// Device side
const ctx = mpc.init();
const dkgA = mpc.dkgNew(ctx, 'device', keyIdBytes, sessionId);
let msgA = undefined, msgB = undefined, doneA=false, doneB=false;

// Server side (in tests we run in-process; in prod it’s coordinator)
const dkgB = mpc.dkgNew(ctx, 'server', keyIdBytes, sessionId);

while (!doneA || !doneB) {
  const stepA = mpc.dkgStep(ctx, dkgA, msgB);
  msgA = stepA.outMsg; doneA = stepA.done;

  const stepB = mpc.dkgStep(ctx, dkgB, msgA);
  msgB = stepB.outMsg; doneB = stepB.done;
}
const kpA = mpc.dkgFinalize(ctx, dkgA);
const kpB = mpc.dkgFinalize(ctx, dkgB);

const signA = mpc.signNew(ctx, kpA, { sessionId });
mpc.signSetMessage(ctx, signA, signBytes);
const signB = mpc.signNew(ctx, kpB, { sessionId });
mpc.signSetMessage(ctx, signB, signBytes);

let sA, sB, doneSA=false, doneSB=false;
while (!doneSA || !doneSB) {
  sA = mpc.signStep(ctx, signA, sB?.outMsg);
  sB = mpc.signStep(ctx, signB, sA?.outMsg);
  doneSA = sA.done; doneSB = sB.done;
}
const sig = mpc.signFinalize(ctx, signA, 'der'); // or raw-rs
